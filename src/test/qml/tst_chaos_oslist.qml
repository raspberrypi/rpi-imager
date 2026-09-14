/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The OS selection screen, rendering a list it did not choose.
 *
 * The OS list is the one piece of untrusted input the interface draws
 * directly: JSON from a repository URL the user can set to anything. The
 * parser is fuzzed. What is not covered is what the interface does when
 * that data is odd but valid -- a thousand-character name, an entry with no
 * name, five hundred subitems, an icon nothing can fetch.
 *
 * Same three locks: nothing may put a device within reach.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosOsList"
    when: windowShown
    width: 900
    height: 700
    visible: true

    property var wiz: null
    property int rngState: 1
    property bool writeStarted: false
    property int labelsRead: 0
    property int figuresRead: 0
    property string savedLanguage: ""

    // Read by cleanup() rather than by the end of the storm. A test function
    // that never returns still has its cleanup() run, and this suite has
    // been abandoned mid-loop before: the checks that sat after the loop
    // went with it and the case reported a pass with nothing behind it.
    property int stepsRun: 0
    property int clicksLanded: 0
    property int flipsMade: 0
    property int stepsUnavailable: 0
    property int widestTree: 0

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
    }

    Component {
        id: containerComponent
        WizardContainer {}
    }

    Item {
        id: overlayRoot
        anchors.fill: parent
    }

    // Math.imul, not *: the product reaches 2.4e18, which is 263 times
    // past the largest integer a double holds exactly, so the multiply
    // rounded before the mask ever ran. The sequence stopped being the
    // LCG it was written as at the second draw, and consecutive draws
    // correlated -- eleven jumps in a row chose the same destination of
    // twelve on one seed, which is what found this.
    function rnd() {
        rngState = (Math.imul(rngState, 1103515245) + 12345) & 0x7fffffff;
        return rngState / 0x7fffffff;
    }

    function rndInt(n) { return n > 0 ? Math.floor(rnd() * n) % n : 0; }

    // A list no repository would serve, every field still the right type.
    // Anything malformed enough to be rejected by the parser would never
    // reach the interface, and the interface is the subject here.
    function hostileOsList() {
        var longName = "";
        for (var i = 0; i < 40; ++i)
            longName += "an-extremely-long-operating-system-name-";

        // Five hundred of them, because breadth is the point -- but the
        // hostile fields are spread thinly across that. Every entry with an
        // invalid init_format is pruned with a warning naming it, so a
        // quarter of them carrying one, and every seventh carrying a
        // sixteen-hundred-character name, printed megabytes on every reload.
        // A click that opened this category then spent long enough inside
        // its nested event loop that the test function was abandoned
        // mid-storm -- the remaining steps and every closing check with it,
        // reported as a pass. One of each says the same thing.
        var subitems = [];
        for (var j = 0; j < 500; ++j) {
            subitems.push({
                name: "sub " + j + (j === 7 ? longName : ""),
                description: j % 3 === 0 ? "" : "subitem description " + j,
                url: "https://example.invalid/sub" + j + ".img.xz",
                // Repository-side forms, not the model's output. Every icon
                // here used to be one the model had already routed, so the
                // storm written to attack the OS list could not reach the
                // routing -- which is where a nested remote icon was handed
                // to Image.source raw and fetched by Qt Quick. example.invalid
                // never resolves, so none of these is a real request.
                icon: (j % 8 === 0) ? "image://icons/https://example.invalid/i.png"
                    : (j % 8 === 1) ? "data:image/png;base64,AAAA"
                    : (j % 8 === 2) ? "ftp://example.invalid/i.png"
                    : (j % 8 === 3) ? ""
                    : (j % 8 === 4) ? "https://example.invalid/bare.png"
                    : (j % 8 === 5) ? "HTTPS://example.invalid/upper.png"
                    : (j % 8 === 6) ? "file://example.invalid/share/unc.png"
                    : "icons/local.png",
                image_download_size: 1,
                extract_size: 1,
                init_format: (j === 3) ? "not-a-format"
                          : (j % 3 === 0) ? "systemd"
                          : (j % 3 === 1) ? "cloudinit" : "none",
                architecture: (j % 2) ? "arm64" : "armhf"
            });
        }

        var top = [
            { name: longName, description: longName,
              url: "https://example.invalid/long.img.xz",
              image_download_size: 1, extract_size: 1, init_format: "systemd" },
            // No name, no url: every field the delegate reads is absent.
            { description: "an entry with almost nothing in it" },
            { name: "deep category", description: "five hundred children",
              subitems: subitems },
            { name: "unicode 你好 😀 \u0000 embedded",
              description: "control characters and astral planes",
              url: "https://example.invalid/u.img.xz",
              image_download_size: 1, extract_size: 1 },
            { name: "negative sizes", url: "https://example.invalid/n.img.xz",
              image_download_size: -1, extract_size: -1, init_format: "systemd" }
        ];

        return JSON.stringify({ os_list: top });
    }

    function init() {
        labelsRead = 0;
        figuresRead = 0;
        savedLanguage = ImageWriterSingleton.getCurrentLanguage();
        TestDrives.clear();
        compare(TestDrives.count(), 0, "no drive while the list is stormed");
        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready");
        writeStarted = false;
        stepsRun = 0;
        clicksLanded = 0;
        widestTree = 0;
        flipsMade = 0;
        stepsUnavailable = 0;

        failOnWarning(/TypeError/);
        failOnWarning(/Binding loop/);
        failOnWarning(/is not a function/);
        failOnWarning(/ReferenceError/);
        failOnWarning(/Unable to assign/);
    }

    function cleanup() {
        // Restored per case, not at the end of the file: init() notes the
        // language at the start of each one, so a restore that waited until
        // cleanupTestCase() would put back whatever the *first* case left
        // rather than what was there before any of this ran. English stands
        // in when the locale matched no translation, because nothing takes a
        // translator back off.
        ImageWriterSingleton.changeLanguage(
            savedLanguage.length > 0 ? savedLanguage : "English");
        verify(stepsRun === 150,
               "the storm ran every step (" + stepsRun + " of 150)");
        verify(clicksLanded > 5, "it landed clicks (" + clicksLanded + ")");
        verify(widestTree > 10, "the screen had a tree to walk (" + widestTree + ")");
        // The offline path is only reached by taking the repository away, so
        // a run that stopped doing it would go on passing while testing half
        // of what it says it does.
        verify(flipsMade > 2, "the repository was taken away (" + flipsMade + ")");
        verify(stepsUnavailable > 5,
               "and the screen drew the offline state (" + stepsUnavailable
               + " steps)");
        // What holds on every seed. A stated size is not among them: two
        // seeds in thirty spend the whole run with the repository taken
        // away and draw no row carrying one, and putting the list back at
        // the end does not bring one out either -- the fetch is
        // asynchronous, while OSListModel::reload() re-reads whatever the
        // writer is already holding, which on those seeds is the empty list
        // the missing URL left. So the size check here is breadth, not a
        // guarantee; the guaranteed one is tst_os_selection_step's own
        // case, which builds the list it needs and waits for it.
        verify(labelsRead > 0, "it read what was drawn (" + labelsRead + ")");
        verify(!writeStarted, "and no write began");
    }

    function cleanupTestCase() {
        // Put the repository back, or every file after this one in the run
        // reads the hostile list: one ImageWriter is shared by all of them.
        ImageWriterSingleton.refreshOsListFromDefaultUrl();
        if (wiz) {
            wiz.destroy();
            wiz = null;
        }
        TestDrives.clear();
    }

    function test_storm_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED");
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ];
        return [ { tag: "seed-5", seed: 5 }, { tag: "seed-7793", seed: 7793 } ];
    }

    function test_storm(data) {
        rngState = data.seed;

        // write() hands back a URL already, not a path -- prefixing file://
        // again produced file://file:///tmp/... , which the fetcher quietly
        // declined, and the screen carried on showing the default list.
        const hostileUrl = TestFiles.write("hostile-oslist.json", hostileOsList());
        verify(hostileUrl.length > 0, "the list was written");
        ImageWriterSingleton.refreshOsListFrom(hostileUrl);

        if (!wiz)
            wiz = containerComponent.createObject(testCase);
        verify(wiz !== null, "the wizard was built");
        wiz.overlayRootRef = overlayRoot;
        wiz.jumpToStep(wiz.stepOSSelection);

        // The fetch is asynchronous, so wait for the list rather than a
        // fixed delay: storming an empty screen would pass and prove
        // nothing. The view itself cannot be reached -- it sits behind a
        // SwipeView and a Loader -- and rowCount() is not invokable from
        // here. The writer's own state answers instead: refreshOsListFrom()
        // marks the list unavailable, so when it reports available again the
        // list in hand is the one just fetched.
        tryVerify(function() {
            return ImageWriterSingleton.isOsListUnavailable === false;
        }, 20000, "the hostile list, not the default one, is what is loaded");

        // The list is the most untrusted data the application handles, and
        // nothing here had ever read a word of what it draws: a row saying
        // NaN, or stating a download size it does not have, walked straight
        // past. Read once deliberately before the random walk, so the checks
        // below stand on something on every seed rather than on the seeds
        // that happen to leave a row on screen.
        // Read on every pass of the walk below rather than once here: the
        // view sits behind a Loader as well as a SwipeView and does not
        // exist the moment the list does -- ten seconds of waiting for it
        // straight after the fetch never found it. Something the walk does
        // brings it out. The counters are checked in cleanup(), where a run
        // that never reached a row cannot pass for one that did.

        var clicks = 0;
        var languagePicks = 0;
        var missingFor = 0;
        var missingUrl = "file:///nonexistent/rpi-imager-chaos/no-such-list.json";
        var maxTargets = 0;
        var keys = [Qt.Key_Down, Qt.Key_Up, Qt.Key_Tab, Qt.Key_Backtab,
                    Qt.Key_Return, Qt.Key_Space, Qt.Key_Escape, Qt.Key_A];

        for (var step = 0; step < 150; ++step) {
            var targets = [];
            collect(wiz, targets, 0);
            if (targets.length > maxTargets)
                maxTargets = targets.length;
            verify(targets.length > 0, "something is on screen at step " + step);

            var action = rndInt(12);
            if (action < 4) {
                var it = targets[rndInt(targets.length)];
                if (!isDestructive(it)) {
                    mouseClick(it, rndInt(Math.max(1, Math.floor(it.width))),
                                   rndInt(Math.max(1, Math.floor(it.height))));
                    ++clicks;
                }
            } else if (action < 8) {
                keyClick(keys[rndInt(keys.length)]);
            } else if (action < 10) {
                wiz.width = 680 + rndInt(600);
                wiz.height = 420 + rndInt(400);
            } else if (action < 11) {
                // changeLanguage() emits osListPrepared(), because the list
                // carries localised entries -- so this reloads the hostile
                // list underneath the screen drawing it, which is the one
                // way this suite can make the model change mid-storm.
                var langs = ImageWriterSingleton.getTranslations();
                if (langs && langs.length > 0) {
                    ImageWriterSingleton.changeLanguage(langs[rndInt(langs.length)]);
                    ++languagePicks;
                }
            } else {
                // Take the list away and give it back. A repository that
                // cannot be read empties the model and raises the offline
                // banner, and coming back forces a full reload where an
                // ordinary arrival only refreshes the rows already there.
                // Neither is reached with anything on screen otherwise.
                //
                // A file:// URL that does not exist, so nothing is fetched.
                // For a few steps only: left to alternate freely the list
                // was gone for half the run.
                // The transitions are the new part, not the empty screen.
                if (missingFor === 0) {
                    ImageWriterSingleton.refreshOsListFrom(missingUrl);
                    missingFor = 2 + rndInt(4);
                    ++flipsMade;
                }
            }

            if (missingFor > 0 && --missingFor === 0)
                ImageWriterSingleton.refreshOsListFrom(hostileUrl);

            // Back to the list if a selection carried the wizard onward: the
            // subject is the screen that draws the list, not the rest.
            if (wiz.currentStep !== wiz.stepOSSelection)
                wiz.jumpToStep(wiz.stepOSSelection);

            examineLabels();
            verify(!writeStarted, "no write began at step " + step);
            compare(TestDrives.count(), 0, "the drive list stayed empty");
            verify(!ImageWriterSingleton.readyToWrite(), "the writer stayed unready");
            if (ImageWriterSingleton.isOsListUnavailable)
                ++stepsUnavailable;
            stepsRun = step + 1;
            clicksLanded = clicks;
            widestTree = maxTargets;
        }

        // Whatever the storm left, the list is put back before the checks
        // below: a case that ended on the missing URL would otherwise leave
        // the next one storming an empty screen.
        ImageWriterSingleton.refreshOsListFrom(hostileUrl);
        tryVerify(function() {
            return ImageWriterSingleton.isOsListUnavailable === false;
        }, 20000, "the hostile list is back");

        // One more read with the list back, which costs nothing and catches
        // a row drawn only once the repository returns.
        waitForRendering(wiz, 5000);
        examineLabels();

        console.log("ChaosOsList seed", data.seed, "clicks", clicks,
                    "widest tree", maxTargets, "language picks", languagePicks,
                    "repository flips", flipsMade,
                    "steps unavailable", stepsUnavailable);
    }

    // Every figure and every word the list puts on screen has to be one a
    // person could act on. A capacity that arrived as a string, a size the
    // repository made up, and a name that was never there all come out here.
    function examineLabels() {
        var texts = [];
        // The rows are not reachable by walking the wizard: the view sits
        // behind a SwipeView and a Loader, and a walk from the top returns
        // the chrome around it -- "Next", "Back", the step names -- and not
        // one word of the list. Asked for by name instead.
        var view = findChild(wiz, "osList");
        if (view) {
            for (var r = 0; r < view.count && r < 12; ++r)
                collectText(view.itemAtIndex(r), texts, 0);
        }
        collectText(wiz, texts, 0);
        for (var i = 0; i < texts.length; ++i) {
            var raw = String(texts[i]);
            var low = raw.toLowerCase();
            ++labelsRead;
            verify(low.indexOf("nan") < 0, "a figure read as NaN: " + raw);
            verify(low.indexOf("infinity") < 0, "a figure ran away: " + raw);
            verify(low.indexOf("undefined") < 0,
                   "a label read as undefined: " + raw);
            // A stated download size has to be one somebody could wait for.
            // The list declares these, and a declaration can be anything.
            var m = raw.match(/([\d.]+)\s*([KMGTP]?B)\s+download/);
            if (m) {
                ++figuresRead;
                verify(parseFloat(m[1]) > 0,
                       "no download stated as nothing: " + raw);
            }
        }
    }

    function collectText(item, out, depth) {
        if (!item || depth > 12 || out.length > 120)
            return;
        if (item.visible === false)
            return;
        if (typeof item.text === "string" && item.text.length > 0)
            out.push(item.text);
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collectText(kids[i], out, depth + 1);
    }

    function collect(item, out, depth) {
        if (!item || depth > 12 || out.length > 300)
            return;
        if (item.visible === false)
            return;
        if (item.width > 0 && item.height > 0)
            out.push(item);
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collect(kids[i], out, depth + 1);
    }

    function isDestructive(item) {
        var t = (item.text !== undefined && typeof item.text === "string")
                ? item.text.toLowerCase() : "";
        var n = (item.objectName || "").toLowerCase();
        return t.indexOf("write") >= 0 || t.indexOf("erase") >= 0
            || t.indexOf("format") >= 0 || n.indexOf("write") >= 0
            || n.indexOf("erase") >= 0 || n.indexOf("format") >= 0;
    }
}
