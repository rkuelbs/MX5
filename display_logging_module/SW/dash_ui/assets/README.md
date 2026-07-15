# Dash Assets

Place fixed-resolution page backgrounds under `backgrounds/`, needles under
`needles/`, and reusable icons under `icons/`. Add production assets to the
`RESOURCES` list in `dash_ui/CMakeLists.txt` so they are embedded in the dash
executable and available before external storage is mounted.
