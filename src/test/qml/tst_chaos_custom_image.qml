/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A custom image that is not what its name says.
 *
 * Choosing a file off the disk is the one place the user hands the
 * application bytes nobody vetted, and the bytes rather than the name decide
 * what happens: write verbatim or unpack, and the size the capacity check
 * and progress bar use. Going by extension let xz bytes named .img be
 * written decompressed -- progress past 400%.
 *
 * The C++ side is fuzzed; the screen was not covered at all.
 *
 * Same three locks: no device within reach, no write begun. The files are
 * a few bytes in a scratch directory.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosCustomImage"
    when: windowShown
    width: 900
    height: 700
    visible: true

    property var wiz: null
    property int rngState: 1
    property bool writeStarted: false
    property bool stormFinished: false
    property int filesAccepted: 0
    property int distinctSizes: 0

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

    function bytes(str) {
        var out = [];
        for (var i = 0; i < str.length; ++i)
            out.push(str.charCodeAt(i) & 0xFF);
        return out;
    }

    function repeated(value, n) {
        var out = [];
        for (var i = 0; i < n; ++i)
            out.push(value);
        return out;
    }

    // Files whose name and contents disagree, or whose contents run out.
    // Written as raw bytes rather than text: the magic numbers are the whole
    // point, and UTF-8 encoding a string turns 1f 8b into 1f c2 8b.
    function hostileFiles() {
        var out = [];

        // Nothing at all, and almost nothing.
        out.push({ name: "empty.img", body: [] });
        out.push({ name: "one-byte.img", body: [0x00] });

        // A gzip header and then nothing that follows one. The trailer the
        // size comes from is not there.
        out.push({ name: "truncated.img.gz",
                   body: [0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03] });

        // An xz header with no index behind it, named as though it were raw.
        out.push({ name: "xz-bytes-named-img.img",
                   body: [0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04].concat(repeated(0x00, 24)) });

        // Zip and tar magic under an image's name: containers claiming to be
        // the thing they would contain.
        out.push({ name: "zip-named-img.img",
                   body: [0x50, 0x4b, 0x03, 0x04].concat(repeated(0x00, 26)) });
        out.push({ name: "tar-named-img.img",
                   body: bytes("ustar-ish").concat(repeated(0x00, 248))
                             .concat(bytes("ustar  ")).concat(repeated(0x00, 256)) });

        // Text under every name the chooser offers, so the extension and the
        // contents disagree in both directions.
        out.push({ name: "prose.img", body: bytes("this is not a disk image at all\n") });
        out.push({ name: "prose.img.xz", body: bytes("nor is this\n") });
        out.push({ name: "prose.zip", body: bytes("nor this\n") });

        // Something big enough to be sized, holding nothing structured.
        out.push({ name: "noise.img", body: repeated(0xA5, 4096) });

        // And four that really are what they claim, so the branches behind
        // "this is a compressed image" are reached rather than only the one
        // for bytes nothing recognises. Empty ones too: a container holding
        // nothing still has a header, and its size is zero, which is the
        // value a capacity check must not divide by.
        //
        // Emitted by gzip and xz and pasted in. There is no way to build
        // either from QML, and an approximation would take the same branch
        // as the noise above, which is the branch already covered.
        out.push({ name: "real-empty.img.gz",
                   body: [31,139,8,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0] });
        out.push({ name: "real-payload.img.gz",
                   body: [31,139,8,0,0,0,0,0,0,3,203,200,84,40,201,72,45,74,213,1,
                          82,153,197,10,64,148,168,80,146,153,87,169,80,144,88,153,
                          147,159,152,2,0,202,225,59,34,32,0,0,0] });
        out.push({ name: "real-empty.img.xz",
                   body: [253,55,122,88,90,0,0,4,230,214,180,70,0,0,0,0,28,223,68,
                          33,31,182,243,125,1,0,0,0,0,4,89,90] });
        out.push({ name: "real-payload.img.xz",
                   body: [253,55,122,88,90,0,0,4,230,214,180,70,4,192,36,32,33,1,22,
                          0,0,0,0,0,0,0,0,0,218,137,234,179,1,0,31,104,105,32,116,
                          104,101,114,101,44,32,116,104,105,115,32,105,115,32,97,32,
                          116,105,110,121,32,112,97,121,108,111,97,100,0,207,14,9,6,
                          217,48,88,128,0,1,64,32,230,218,145,235,31,182,243,125,1,
                          0,0,0,0,4,89,90] });

        return out;
    }

    function init() {
        TestDrives.clear();
        compare(TestDrives.count(), 0, "no drive while a custom image is chosen");
        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready");
        writeStarted = false;
        stormFinished = false;
        filesAccepted = 0;
        distinctSizes = 0;

        failOnWarning(/TypeError/);
        failOnWarning(/ReferenceError/);
        failOnWarning(/Binding loop/);
        failOnWarning(/is not a function/);
        failOnWarning(/Unable to assign/);
    }

    function cleanup() {
        // Checked here rather than after the loop: a test function can be
        // abandoned part-way and everything written after its loop goes with
        // it, leaving a pass that stands for nothing.
        verify(stormFinished, "the storm reached the end of its loop");
        verify(filesAccepted >= 14, "every file was offered (" + filesAccepted + ")");
        verify(distinctSizes > 1,
               "and they were not all sized the same (" + distinctSizes + ")");
        verify(!writeStarted, "and no write began");
        TestDrives.clear();
        // The files above are sources on the writer every other file shares.
        // Left there, the next file starts halfway to ready.
        ImageWriterSingleton.setSrc("");
        ImageWriterSingleton.setDst("");
    }

    function cleanupTestCase() {
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
        return [ { tag: "seed-17", seed: 17 }, { tag: "seed-5009", seed: 5009 } ];
    }

    function test_storm(data) {
        rngState = data.seed;

        if (!wiz)
            wiz = containerComponent.createObject(testCase);
        verify(wiz !== null, "the wizard was built");
        wiz.overlayRootRef = overlayRoot;
        wiz.jumpToStep(wiz.stepOSSelection);

        var files = hostileFiles();
        var keys = [Qt.Key_Down, Qt.Key_Up, Qt.Key_Tab, Qt.Key_Backtab,
                    Qt.Key_Return, Qt.Key_Space, Qt.Key_Escape];
        var sizesSeen = ({});
        var clicks = 0;

        for (var f = 0; f < files.length; ++f) {
            const url = TestFiles.writeBytes(files[f].name, files[f].body);
            verify(url.length > 0, "wrote " + files[f].name);

            // The path the file chooser takes when it comes back with a
            // selection, without opening a chooser: a native dialog here
            // would be a modal nothing can dismiss.
            ImageWriterSingleton.acceptCustomImageFromQml(url);
            ++filesAccepted;

            // Whatever was made of it, the size has to be a number the rest
            // of the application can use. A capacity check divides by it and
            // a progress bar is drawn from it.
            var size = ImageWriterSingleton.getSelectedSourceSize();
            verify(size >= 0, "the size is not negative for " + files[f].name);
            verify(size < 1024 * 1024 * 1024 * 1024,
                   "nor absurd for " + files[f].name + " (" + size + ")");
            sizesSeen[String(size)] = true;

            // Then storm the screen that is now showing it.
            for (var step = 0; step < 12; ++step) {
                var targets = [];
                collect(wiz, targets, 0);
                verify(targets.length > 0, "something is on screen");

                var action = rndInt(8);
                if (action < 3) {
                    var it = targets[rndInt(targets.length)];
                    if (!isDestructive(it)) {
                        mouseClick(it, rndInt(Math.max(1, Math.floor(it.width))),
                                       rndInt(Math.max(1, Math.floor(it.height))));
                        ++clicks;
                    }
                } else if (action < 6) {
                    keyClick(keys[rndInt(keys.length)]);
                } else {
                    wiz.width = 700 + rndInt(500);
                    wiz.height = 440 + rndInt(300);
                }

                // Back to the screen under test if a selection moved on.
                if (wiz.currentStep !== wiz.stepOSSelection)
                    wiz.jumpToStep(wiz.stepOSSelection);

                verify(!writeStarted, "no write began");
                compare(TestDrives.count(), 0, "the drive list stayed empty");
                verify(!ImageWriterSingleton.readyToWrite(),
                       "the writer stayed unready");
            }
        }

        distinctSizes = Object.keys(sizesSeen).length;
        stormFinished = true;

        console.log("ChaosCustomImage seed", data.seed, "files", filesAccepted,
                    "clicks", clicks, "distinct sizes", distinctSizes,
                    JSON.stringify(Object.keys(sizesSeen)));
    }

    function collect(item, out, depth) {
        if (!item || depth > 12 || out.length > 250)
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
