{
    "target_defaults": {
      "include_dirs": [
        "bklib",
        "/opt/local/include",
        "<!(node -e \"require('nan')\")"
      ]
    },
    "targets": [
    {
      "target_name": "binding",
      "sources": [
        "binding.cpp",
        "bklib/bklib.cpp",
        "bklib/bklog.cpp",
      ],
      "conditions": [
        [ 'OS=="mac"', {
          "defines": [
            "OS_MACOSX",
          ],
          "xcode_settings": {
            "OTHER_CFLAGS": [
              "-g -fPIC",
            ],
          },
        }],
        [ 'OS=="linux"', {
          "defines": [
            "OS_LINUX",
          ],
          "cflags_cc+": [
            "-g -fPIC -rdynamic",
          ],
        }],
      ]
    }]
}
