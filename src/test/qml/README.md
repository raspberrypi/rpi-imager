# The QML storms

`tst_chaos_*.qml` drive the interface the way no ordered test does: a
pseudo-random sequence of clicks, keys, resizes and state changes, asking not
that a particular thing happens but that some property never stops holding.
Twelve of them, each covering a surface the others do not — the wizard, its
dialogs, the three lists it draws from untrusted data, the keyboard ring, the
window's shape, the wizard being torn down while it is busy, and a write
already under way.

Eleven are locked the same three ways: no drive is ever within reach, no
write is ever begun, and no key that confirms anything is sent where a
confirmation could be destructive. They run against the shipping components,
not stubs.

`tst_chaos_writing` is the exception to the second lock and needs reading
before it is changed. Its whole subject is a run in progress, so it cannot
assert that no write began — instead it never lets one exist. The step is
built against a stub container, so the wizard's write path is not present;
no source and no destination are ever set, which is checked on every pass of
the loop; and the storm calls the step's own progress handlers rather than
anything that writes. Nothing in that file can reach a device. Keep it that
way: a storm that drives the real writer would be a storm that can erase a
disk.

## Seeds

Each storm ships with two seeds, so an ordinary run is quick and a failure is
reproducible. `RPI_CHAOS_SEED=<n>` replaces them with one, which is how a
failure reported by the suite is replayed.

Two sequences are not many, and the same door lets a sweep walk a lot more:

    src/test/qml/seed_sweep.sh build-uitest/test/qml_ui_test

Point it at a sanitised build and the two techniques compose, which is what
a lifetime fault on a rarely-taken path needs — the seeds to reach it and the
sanitiser to see it:

    src/test/qml/seed_sweep.sh build-asan/test/qml_ui_test
    UBSAN_OPTIONS=halt_on_error=1 \
        src/test/qml/seed_sweep.sh build-ubsan/test/qml_ui_test

`halt_on_error=1` makes a UBSan finding abort the storm, so the seed fails
unambiguously. It used to be the only thing that worked: GCC's UBSan prints a
report and carries on, and the sweep read the exit status and a `FAIL` line,
neither of which a diagnostic produces. The sweep now also greps for
`runtime error:`, so `halt_on_error=0` is a defensible alternative — one run
then reports every site it reaches instead of stopping at the first. The ASan
sweep reads `lsan-qt.supp`, without which thirteen seeds in thirty report
fontconfig's font fallback rather than anything of ours.

Two controls worth knowing:

    RPI_SWEEP_TIMEOUT=900    # seconds per seed; 600 by default
    RPI_SWEEP_KEEP=<dir>     # write each failure's whole output there

The budget exists because one seed ran 2563 seconds at full CPU against about
eight for its twenty-nine siblings, and held the sweep behind it for
forty-two minutes. `RPI_SWEEP_KEEP` exists because the summary keeps five
grepped lines, and a sanitiser report is thirty frames — twice in one session
a failure could not be explained afterwards and did not reproduce on demand.

It puts every XDG directory in a scratch tree beside the binary and removes
it afterwards. The suites already ask Qt for test-mode paths, which keeps
them clear of the real application's settings, but test mode still lands
under the home directory and a sweep is hundreds of runs -- two hundred and
twenty of them took about 190 MB.

What a sweep is for is not finding more defects in the application — it is
finding storms that prove less than they claim. A storm asserts something
like "no system drive became the destination", which says nothing at all on a
run where nothing was chosen. Every suite carries a guard against exactly
that (`verify(selectionsSeen > 0, ...)` and its kin), and a guard only fires
on the seeds that reach it.

Two sweeps of ten seeds found two such storms, both since fixed:

- **`tst_chaos_storage`** chose no drive at all on four seeds in ten, so the
  check the file exists for held over an empty set. It now makes one
  deliberate choice before the random ones.
- **`tst_chaos_monkey`** saw the screen-reader flag one way only on two seeds
  in ten. The flag is polled every 500 ms rather than signalled, so a flip
  recorded straight after making it records the value from before. Both
  states are now shown deliberately, waiting for the poll each time.

A third sweep, of all twelve storms across thirty seeds under both
sanitisers -- 720 runs -- found no storm proving less than it claimed:
UBSan reported nothing at all, and ASan reported one failure, in
`tst_chaos_custom_image` at seed 100523, which carried no FAIL line and
no sanitiser report and did not reproduce in thirty-four further runs
with the machine quieter. It is recorded here rather than explained.
That sweep is also why a failure with nothing matching now prints the
last few lines: there was nothing to look at.

The second was not reproducible on demand afterwards, which suggests it
depended on machine load as well as the seed — the poll has to fire during
the walk for the old behaviour to record anything. The fix removes the
dependence either way.

## Writing one

The things that have gone wrong in these, in the order they cost the most
time:

- **A guard in `cleanupTestCase()` accumulates; a guard in `cleanup()` does
  not.** The end-of-file hook runs once, against totals summed over every
  case, so one case satisfies it for all of them. `tst_chaos_customisation`
  guarded on a running *maximum*: once any case had committed a setting,
  every later empty one passed, and a change breaking the commit path
  everywhere would have left a single case carrying the guard for
  forty-two. `tst_chaos_dialogs` required no dialog to have opened at all,
  which is where the modality check it exists for lives.

  Both now count cases and hold them to a majority or to all. Of the twelve
  storms, three keep guards in `cleanupTestCase()` and two of those were
  weak; the rest guard per case, which cannot be satisfied by a sibling.
  Where assertions sit after the loop instead, `verify(stormFinished, ...)`
  in `cleanup()` is what stops an abandoned run skipping them.

- **Walking a screen is not reading it.** A storm that clicks and resizes
  proves the interface survives; it says nothing about what the interface
  said. `tst_chaos_oslist` ran for 150 steps over the least trusted data in
  the application and never read a word of it, and a row stating a download
  size it did not have went straight past. Reading from the top does not
  fix it either: the list sits behind a SwipeView and a Loader, so a walk
  from the wizard returned 3469 labels of chrome -- "Next", "Back", the step
  names -- and no row content at all. Ask for the view by name.

  Four storms draw figures out of data they do not control, and all four now
  check them. The rest draw no figures, and adding the check there would
  only look like coverage.

- **A storm can be abandoned mid-loop and still report a pass.** QtTest stops
  the function and runs `cleanup()`; everything written after the loop goes
  with it. Put the assertions in `cleanup()`, reading counters the loop
  updates, and assert the loop reached its end.
- **A check that never runs passes.** Count what the storm actually did and
  assert the count, or the property stands on nothing. That is what the
  sweep above looks for.
- **One ImageWriter serves every file in the run.** A storm that leaves a
  source, a destination, a repository or a language behind hands it to
  whichever file runs next.
- **A measurement taken at the wrong moment accuses the innocent.** A control
  is unsized until it has been laid out, and scrolled out of view until
  focused; a list has rows in its model before the view has made any of them.
  Measure after the thing settles, and while the control is the one in hand.
- **Write hostile strings as escapes.** A literal control byte makes the
  whole file binary: `file(1)` calls it data and `grep` finds nothing in it
  by finding nothing.
