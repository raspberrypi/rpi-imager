/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Random text and keys at the customisation steps, checking what they store.
 *
 * The wizard run rarely reaches these: they sit several steps in, behind
 * choices a random click seldom makes in order. Each is built here on its
 * own and stormed directly.
 *
 * It asserts more than survival. These fields are where a user's text turns
 * into settings, and those settings become firstrun.sh on a card that boots
 * unattended. So every stored value is checked against the contract its
 * field advertises.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosCustomisation"
    when: windowShown
    width: 900
    height: 700
    visible: true

    property int rngState: 1
    property bool writeStarted: false
    property string savedLanguage: ""
    property int restoredCases: 0
    property int restoredChecks: 0
    // Totalled across the file and checked once at the end, rather than per
    // case: a case can end early with none of these, but a run where the
    // dimension stopped firing altogether would otherwise go on passing
    // while testing one fewer thing than it says it does.
    property int languagePicksTotal: 0

    // Set when the storm's loop is left, and checked in cleanup() rather
    // than after the loop. A test function can be abandoned part-way -- a
    // step that spends long enough inside a nested event loop is enough --
    // and every check written after the loop goes with it, leaving a pass
    // that stands for nothing. cleanup() still runs.
    property bool stormFinished: false


    // Most settings any one case managed to store. Checked once at the end
    // rather than per case: an individual seed can legitimately leave every
    // field empty, but the suite as a whole going quiet means the checks in
    // checkStoredSettings() are iterating nothing.
    property int mostStored: 0
    property int casesRun: 0
    property int casesThatStored: 0

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
    }

    Item {
        id: overlayRoot
        anchors.fill: parent
    }

    // The union of what the individual step tests provide. A step reading a
    // property the container does not have warns rather than fails, so a
    // missing one would quietly weaken the run instead of stopping it.
    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property bool passwordlessSudoAvailable: true
        property bool disableWarnings: false
        property bool ccRpiAvailable: true
        property bool ifAndFeaturesAvailable: true
        property bool ifI2cEnabled: false
        property bool ifSpiEnabled: false
        property bool if1WireEnabled: false
        property bool featUsbGadgetEnabled: false
        property string ifSerial: "Disabled"
        property string selectedDeviceName: "Raspberry Pi 5"
        property string networkInfoText: ""
        property Item overlayRootRef: overlayRoot
        property int stepWriting: 9
        property int jumpedTo: -1
        property int skipAllCalls: 0

        // Everything the four steps added below reach for. A step reading a
        // property the container has not got sees undefined and throws on the
        // first use of it -- which reads exactly like a defect in the step,
        // and is not one. Taken from WizardContainer.qml rather than guessed.
        property string connectOrgApiKey: ""
        property string connectOrgDescription: ""
        property bool otpProvisioningEnabled: false
        property bool secureBootEnabled: false
        property bool secureBootAvailable: true
        property bool secureBootKeyConfigured: false
        property bool piConnectEnabled: false
        property bool piConnectAvailable: true
        property bool targetIsFastboot: false
        property int nextCalls: 0

        function jumpToStep(n) { jumpedTo = n }
        function skipAllCustomisation() { skipAllCalls++ }
        function nextStep() { nextCalls++ }
    }

    Component { id: hostnameComponent; HostnameCustomizationStep { wizardContainer: fakeContainer; width: 900; height: 700 } }
    Component { id: userComponent;     UserCustomizationStep     { wizardContainer: fakeContainer; width: 900; height: 700 } }
    Component { id: wifiComponent;     WifiCustomizationStep     { wizardContainer: fakeContainer; width: 900; height: 700 } }
    Component { id: localeComponent;   LocaleCustomizationStep   { wizardContainer: fakeContainer; width: 900; height: 700 } }
    Component { id: remoteComponent;   RemoteAccessStep          { wizardContainer: fakeContainer; width: 900; height: 700 } }
    Component { id: ifFeatComponent;   IfAndFeaturesCustomizationStep { wizardContainer: fakeContainer; width: 900; height: 700 } }
    Component { id: secureBootComponent; SecureBootCustomizationStep  { wizardContainer: fakeContainer; width: 900; height: 700 } }
    // No PiConnect component, and deliberately. Its controls call
    // requestOrgAuthKey(), verifyAuthKey(), overwriteConnectToken() and
    // setConnectOrgRegistration() -- minting a credential and registering a
    // device with a service. RPI_IMAGER_CONNECT_URL points at a dead port in
    // the test environment so they would fail, but a storm clicking buttons
    // that enrol hardware is not something to make safe by where a URL
    // happens to point. Its fields are covered by tst_pi_connect*.qml, which
    // press the ones they mean to.

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

    // Text a field is not expecting. The shell metacharacters matter because
    // these values are read back by a shell on first boot; the newline and
    // the NUL because the generator's callers split on lines.
    readonly property var nasty: [
        "a'b\"c", "$(id)", "`id`", "x;rm -rf /", "..\\..\\etc",
        "line\nbreak", "tab\there", "nul\u0000byte", "éèê",
        "你好世界", "😀", "  padded  ",
        "-leading-hyphen", "trailing-hyphen-",
        "0123456789012345678901234567890123456789012345678901234567890123456789",
        "", " ", "\\", "%s%s%s%n", "<script>alert(1)</script>",

        // Shapes fuzz_customisation's corpus is made of. Coverage-guided
        // fuzzing kept these because they reach code the shorter cases do
        // not: shellQuote() wraps its argument in single quotes and has to
        // break out of them for every one it contains, so a run of quotes is
        // where that logic actually gets worked. The hand-written entries
        // above have one quote each and never found it.
        "''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''",
        "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"",
        "'\\'\\'\\'\\'\\'\\'\\'\\'\\'\\'\\'\\'",
        "''''''''''$(id)''''''''''`id`''''''''''",
        "0000000000000000000000:0000000000000000000000000'0000000000"
    ]

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

    // A field is anything that will take a cursor. Duck-typed rather than
    // matched by type, because the wizard's inputs are wrappers around
    // TextField rather than TextField itself.
    // Find a named control anywhere under the step.
    function byName(root, name) {
        var all = [];
        collect(root, all, 0);
        for (var i = 0; i < all.length; ++i)
            if (all[i].objectName === name)
                return all[i];
        return null;
    }

    // The user step only commits when the two password fields agree, and
    // random text never makes them agree -- so the storm reached its commit
    // path zero times out of three seeds and every assertion about what it
    // stored was checking an empty object. This makes them match, with the
    // same adversarial text, so the hashing path actually runs.
    function mirrorPasswords(step, text) {
        var pw = byName(step, "userPasswordField");
        var cf = byName(step, "userPasswordConfirmField");
        var un = byName(step, "userNameField");
        if (!pw || !cf)
            return false;
        if (un && un.text.length === 0)
            un.text = "pi";
        pw.text = text;
        cf.text = text;
        return true;
    }

    // Two steps only commit when their enable toggle is on, and random
    // clicking rarely lands on it -- so they stored nothing on every seed and
    // the checks below had no subject, exactly as the user step did.
    //
    // PiConnect is deliberately left out. Its commit path mints an
    // organisation auth key, and driving an enrolment with random input is
    // not something a chaos suite should do, whatever the endpoint is set to.
    function enableStepToggle(step) {
        var names = ["sshEnableToggle", "secureBootEnablePill"];
        for (var i = 0; i < names.length; ++i) {
            var t = byName(step, names[i]);
            if (t && t.checked !== undefined && !t.checked) {
                t.checked = true;
                return true;
            }
        }
        return false;
    }

    function isField(item) {
        return item.text !== undefined
            && typeof item.text === "string"
            && item.activeFocusOnTab !== undefined
            && item.readOnly !== true;
    }

    // Settings from a session nobody had: values no field could produce.
    //
    // A step restores these into its controls, and assigning text does not
    // run a validator -- Qt only clears acceptableInput, which nothing reads.
    // Two steps were writing such a value straight back out on Next, and the
    // username becomes the account name in firstrun.sh, run as root. This is
    // what keeps a third from appearing.
    // sshAuthorizedKeys below is the shape that turned a text box into a
    // command in firstrun.sh: a line equal to a here-document's delimiter
    // closed it early and left the rest as script. It is not something a
    // step should refuse -- a key file is legitimately several lines -- so
    // what is asked here is only that the path carries it. The safety sits
    // in the generator, which picks a delimiter the content does not hold.
    //
    // The hostname keeps its old value. Given a newline it travels intact
    // through every step that does not own it, which is every step but one,
    // and the check below would have to stop looking at newlines in
    // single-line fields to allow it -- for a hazard the generator already
    // takes out and has its own tests for.
    readonly property var hostileSaved: ({
        hostname: "-starts-with-a-hyphen",
        sshUserName: "Root User",
        wifiSSID: "an ssid\u0001with a control character",
        wifiPassword: "x".repeat(300),
        timezone: "Nowhere/Nothing",
        keyboardLayout: "../../etc/passwd",
        sshAuthorizedKeys: "ssh-rsa AAAAB3\nEOF\ntouch /tmp/pwned\n",
        // The one field here whose value reaches the kernel command line,
        // as cfg80211.ieee80211_regdom=. That line is space-separated, so a
        // space in the value is not a bad country -- it is another kernel
        // parameter, and init=/bin/sh is one of those.
        //
        // The picker offers a list, so nothing typed can be this; restored
        // settings are the only way in, which is what this case is. Like
        // sshAuthorizedKeys above, the step is not asked to refuse it: the
        // safety is in the generator, whose sanitisedCountryCode() passes
        // exactly two ASCII letters and otherwise nothing at all.
        wifiCountry: "GB init=/bin/sh",
        secureBootEnabled: "yes please"
    })

    function init() {
        // The language is process-wide and every case in this binary shares
        // one ImageWriter, so it is noted here and put back in cleanup().
        savedLanguage = ImageWriterSingleton.getCurrentLanguage();
        TestDrives.clear();
        compare(TestDrives.count(), 0, "nothing to write to while the fields are stormed");
        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready");
        writeStarted = false;
        fakeContainer.customizationSettings = ({});
        fakeContainer.jumpedTo = -1;

        failOnWarning(/TypeError/);
        failOnWarning(/Binding loop/);
        failOnWarning(/is not a function/);
        failOnWarning(/ReferenceError/);
        failOnWarning(/Unable to assign/);
    }

    function cleanup() {
        verify(stormFinished, "the storm reached the end of its loop");
        // English is the source language, and there is no call that takes a
        // translator back off: it stands in when the system locale matched
        // none and there was nothing to note.
        ImageWriterSingleton.changeLanguage(
            savedLanguage.length > 0 ? savedLanguage : "English");
        TestDrives.clear();
    }

    function cleanupTestCase() {
        // mostStored is a running maximum, so once any case has committed
        // something every later empty one satisfies it. Measured, two cases
        // in forty-two store nothing -- a walk that never presses Next --
        // which is fine; what is not fine is that a change breaking the
        // commit path everywhere would leave one case carrying the guard for
        // all of them. Most of them have to get there.
        verify(casesThatStored * 2 >= casesRun,
               "most cases committed something (" + casesThatStored
               + " of " + casesRun + ")");

        verify(languagePicksTotal > 0,
               "the storm changed language at least once");
        // The user step used to reach its commit path zero times, because it
        // only stores when the two password fields agree and random text never
        // made them agree. Every check on what it stored passed by iterating
        // an empty object. This is what stops that returning unnoticed.
        verify(restoredCases > 0,
               "some case opened on settings no field could have produced");
        verify(restoredChecks > 0,
               "and a field was checked against its own rule (" + restoredChecks + ")");
        verify(mostStored > 0,
               "some case committed settings for the checks to run against ("
               + mostStored + ")");
        TestDrives.clear();
    }

    function test_storm_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED");
        var seeds = override.length > 0 ? [ parseInt(override) ] : [ 3, 2749, 55291 ];
        var out = [];
        var steps = [
            { name: "hostname", comp: hostnameComponent },
            { name: "user",     comp: userComponent },
            { name: "wifi",     comp: wifiComponent },
            { name: "locale",   comp: localeComponent },
            { name: "remote",   comp: remoteComponent },
            { name: "iffeat",   comp: ifFeatComponent },
            { name: "secboot",  comp: secureBootComponent }
        ];
        for (var s = 0; s < steps.length; ++s)
            for (var k = 0; k < seeds.length; ++k) {
                out.push({ tag: steps[s].name + "-seed-" + seeds[k],
                           comp: steps[s].comp, seed: seeds[k],
                           hostile: false });
                // The same step again, opened on settings from a session
                // nobody had.
                out.push({ tag: steps[s].name + "-restored-" + seeds[k],
                           comp: steps[s].comp, seed: seeds[k],
                           hostile: true });
            }
        return out;
    }

    function test_storm(data) {
        rngState = data.seed;

        if (data.hostile) {
            var seeded = ({});
            for (var k in hostileSaved)
                seeded[k] = hostileSaved[k];
            fakeContainer.customizationSettings = seeded;
            ++restoredCases;
        }

        var step = data.comp.createObject(testCase);
        verify(step !== null, "the step was built");

        // Checked before the storm touches anything, because the storm
        // assigns text directly and a validator only filters keystrokes.
        // What is asked here is narrower and real: a value that arrived from
        // saved settings must satisfy the rule its own field advertises, or
        // the step must have refused it. Typing and pasting are already
        // filtered; restoring was not, and two steps wrote such a value back
        // out on Next.
        if (data.hostile)
            checkRestoredAgainstRules(data.tag, step);

        var typed = 0;
        var languagePicks = 0;
        var mirrored = 0;

        // The same naming check the wizard storm carries. This one reaches
        // each step directly rather than navigating to it, so it sees
        // controls the other rarely lands on.
        var namelessFocused = ({});
        var toggled = 0;
        var fieldsSeen = 0;

        stormFinished = false;
        for (var i = 0; i < 120; ++i) {
            var items = [];
            collect(step, items, 0);
            verify(items.length > 0, "the step is on screen at " + i);

            var fields = items.filter(isField);
            if (fields.length > fieldsSeen)
                fieldsSeen = fields.length;

            var action = rndInt(11);
            if (action < 4 && fields.length > 0) {
                // Set the text outright as well as typing it: a validator
                // filters keystrokes, and assigning the property is the path
                // that does not go through one.
                var f = fields[rndInt(fields.length)];
                f.forceActiveFocus();
                if (rndInt(2) === 0) {
                    keyClick(Qt.Key_A + rndInt(26));
                } else {
                    f.text = nasty[rndInt(nasty.length)];
                }
                ++typed;
            } else if (action < 5) {
                // Only does anything on the step that has these; elsewhere it
                // is a no-op and the step below picks up the slack.
                if (mirrorPasswords(step, nasty[rndInt(nasty.length)]))
                    ++mirrored;
                if (enableStepToggle(step))
                    ++toggled;
            } else if (action < 6) {
                keyClick([Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Return,
                          Qt.Key_Escape, Qt.Key_Space, Qt.Key_Down,
                          Qt.Key_Up, Qt.Key_Backspace][rndInt(8)]);
            } else if (action < 8) {
                var t = items[rndInt(items.length)];
                var x = rndInt(Math.max(1, Math.floor(t.width)));
                var y = rndInt(Math.max(1, Math.floor(t.height)));
                mouseClick(t, x, y);
            } else if (action < 9) {
                step.width = 400 + rndInt(700);
                step.height = 320 + rndInt(500);
            } else if (action < 10) {
                // Retranslation re-evaluates every qsTr() binding at once,
                // and this step has more of them than any other -- labels,
                // placeholders, the combo boxes' entries. Typed text is not
                // translated, so what is in the fields has to survive it.
                var langs = ImageWriterSingleton.getTranslations();
                if (langs && langs.length > 0) {
                    ImageWriterSingleton.changeLanguage(langs[rndInt(langs.length)]);
                    ++languagePicks;
                }
            } else if (step.nextClicked !== undefined) {
                // Commit whatever is in the fields. This is the step that
                // turns text into settings, so it is the one worth reaching.
                step.nextClicked();
            }

            var focused = step.Window.activeFocusItem;
            if (focused && focused.activeFocusOnTab === true) {
                var an = focused.Accessible ? (focused.Accessible.name || "") : "";
                var ad = focused.Accessible ? (focused.Accessible.description || "") : "";
                var tx = (typeof focused.text === "string") ? focused.text : "";
                if (an.length === 0 && ad.length === 0 && tx.length === 0)
                    namelessFocused[(focused.objectName || "?") + "/"
                                    + focused.toString().split("(")[0]] = true;
            }

            verify(!writeStarted, "no write began at " + i + " (seed " + data.seed + ")");
            compare(TestDrives.count(), 0, "the drive list stayed empty at " + i);
            verify(!ImageWriterSingleton.readyToWrite(), "the writer stayed unready at " + i);
        }

        ++testCase.casesRun;
        stormFinished = true;

        // Commit once more at the end, so a run whose random actions never
        // reached the branch above still checks what gets stored.
        if (step.nextClicked !== undefined)
            step.nextClicked();

        checkStoredSettings(data.tag);

        languagePicksTotal += languagePicks;
        console.log("ChaosCustomisation", data.tag, "mirrored passwords", mirrored,
                    "language picks", languagePicks,
                    "| nothing to announce:",
                    JSON.stringify(Object.keys(namelessFocused)));
        verify(typed > 10, "text reached the step (" + typed + ")");
        verify(fieldsSeen > 0, "the step offered somewhere to type (" + fieldsSeen + ")");

        step.destroy();
    }

    // Every stored value is read back by a shell on first boot, from a file
    // whose readers split it into lines. A raw newline or NUL in one of them
    // is a setting that lands somewhere it was never meant to go.
    // What a step made of settings it was handed, before any storming.
    function checkRestoredAgainstRules(tag, step) {
        var pairs = [
            { rule: "hostnameRule", field: "hostnameField" },
            { rule: "usernameRule", field: "userNameField" }
        ];
        for (var i = 0; i < pairs.length; ++i) {
            if (step[pairs[i].rule] === undefined)
                continue;
            var f = findChild(step, pairs[i].field);
            if (!f || typeof f.text !== "string")
                continue;
            // Empty counts: refusing the value is the right answer, and it
            // is what the field holds afterwards. Counting only surviving
            // values would leave the check at zero exactly when it passes.
            verify(f.text.length === 0 || step[pairs[i].rule].test(f.text),
                   tag + ": " + pairs[i].field + " was refused or holds "
                   + "something its own rule accepts ("
                   + JSON.stringify(f.text) + ")");
            ++restoredChecks;
        }
    }

    function checkStoredSettings(tag) {
        var s = fakeContainer.customizationSettings;


        // Counted and reported. Every assertion below iterates the stored
        // settings, so a run that stored nothing would satisfy all of them
        // while checking nothing at all -- the same way an empty loop passes.
        var stored = Object.keys(s);
        console.log("ChaosCustomisation", tag, "stored", stored.length,
                    "settings:", JSON.stringify(stored));
        if (stored.length > testCase.mostStored)
            testCase.mostStored = stored.length;
        if (stored.length > 0)
            ++testCase.casesThatStored;

        for (var key in s) {
            var v = s[key];
            if (typeof v !== "string")
                continue;

            // An authorized_keys file is one key per line, and the step says
            // so where it saves: "multi-line string, for consistency". The
            // rule below held for it only because nothing had ever put a
            // newline there -- it was true the way an empty loop is true.
            // What keeps a line of it from becoming a command in firstrun.sh
            // is the generator, which chooses a here-document delimiter the
            // content does not contain.
            if (key !== "sshAuthorizedKeys") {
                verify(v.indexOf("\n") < 0, tag + ": " + key + " carries no newline");
                verify(v.indexOf("\r") < 0, tag + ": " + key + " carries no carriage return");
            }
            verify(v.indexOf("\u0000") < 0, tag + ": " + key + " carries no NUL");
        }

        // Not asserted here: that the stored hostname satisfies the pattern
        // its own validator carries. It does when the text was typed, since
        // a keystroke that could never match is refused. It does not when
        // the text was assigned, and the wizard assigns: settings come off
        // disk at WizardContainer.qml:195, the step prefills the field from
        // them, and onNextClicked stores the value back without consulting
        // acceptableInput. Whether to drop a saved hostname that no longer
        // matches is a behaviour question, so this records the route rather
        // than deciding it.
    }
}
