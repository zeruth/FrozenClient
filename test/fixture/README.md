# Test Fixtures

Some of Frozen's tests require certain assets for the tests to work properly. Since these assets are
not appropriate for redistribution, they must be manually placed here. CI runs in Frozen's source
control platform involve a step that copies these assets into the `fixture` directory without
directly exposing the assets.

Expected fixtures are:

* `font/FRIZQT__.TTF`: The primary UI font used in the game
