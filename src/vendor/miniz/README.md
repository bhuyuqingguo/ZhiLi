# miniz raw inflater

Source: https://github.com/richgel999/miniz
Version: 3.1.2, commit 77d0dce8627735138c51770d1799a1ef48f2117d
License: MIT, included in LICENSE and the source header.

Only miniz_tinfl.c, miniz_tinfl.h and miniz_common.h are vendored. The sole
upstream source change replaces miniz.h with miniz_tinfl.h. miniz_export.h is
local static-build configuration. importer.cpp builds the inflater directly,
with MINIZ_NO_MALLOC and portable byte reads. There is no archive extraction,
external runtime, or extra dynamic-library dependency.

Keep LICENSE with distributed source and application packages.
