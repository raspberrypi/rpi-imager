{
  "targets": [
    {
      "target_name": "MountUtils",
      "include_dirs" : [
        "<!(node -e \"require('nan')\")"
      ],
      "sources": [
        "src/mountutils.cpp",
        "src/worker-unmount.cpp",
        "src/worker-eject.cpp"
      ],
      "msvs_settings": {
        "VCLinkerTool": {
          "SetChecksum": "true"
        },
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "AdditionalOptions": [
            "/EHsc"
          ]
        }
      },
      'conditions': [

        [ 'OS=="linux"', {
          "sources": [
            "src/linux/functions.cpp"
          ],
        } ],

        [ 'OS=="win"', {
          "sources": [
            "src/windows/functions.cpp"
          ],
          "libraries": [
            "-lKernel32.lib",
            "-lSetupAPI.lib",
            "-lCfgmgr32.lib"
          ],
        } ],

        [ 'OS=="mac"', {
          "xcode_settings": {
            "OTHER_CPLUSPLUSFLAGS": [
              "-stdlib=libc++"
            ],
            "OTHER_LDFLAGS": [
              "-stdlib=libc++"
            ]
          },
          "sources": [
            "src/darwin/functions.cpp"
          ],
          "link_settings": {
            "libraries": [
              "DiskArbitration.framework",
              "Foundation.framework",
            ],
          },
        } ],

      ],
    }
  ],
}
