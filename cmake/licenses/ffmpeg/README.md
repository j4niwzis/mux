These files are unchanged copies from FFmpeg's n7.1 source release:

https://github.com/FFmpeg/FFmpeg/tree/n7.1

They supply license text for the About page even when the system package
does not install FFmpeg's source or license files. The page uses the linked
library's `avcodec_license()` result to select the applicable text, rather
than assuming that every system build uses the provider's LGPL default.
LGPL v3 is displayed together with GPL v3, which it incorporates.
