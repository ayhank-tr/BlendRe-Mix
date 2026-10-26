# BGE-Remix: the one RTX Remix runtime this build is made for.
#
# The Remix API version of extern/remix_sdk/remix/remix_c.h must match
# (a runtime with another API minor version refuses to start). Update both
# together, and check the SHA-256 against the downloaded file.
#
# Python 3.5 (Blender 2.79): no f-strings.

VERSION = "1.6.1"
API_VERSION = "0.6.5"
RELEASED = "2026-10-23"

URL = ("https://github.com/NVIDIAGameWorks/rtx-remix/releases/download/"
       "remix-1.6.1/rtx-remix-for-x64-games-release-remix-1.6.1.zip")
SHA256 = "7dd1d44492a609050e44b0e3581cec31135d1ed88b4a91123b4faae91955716a"
SIZE = 557087446  # bytes

LICENSE_NOTE = (
    "The RTX Remix runtime is made by NVIDIA. Its code is MIT licensed; the "
    "NVIDIA libraries it includes (DLSS, NRD, NRC and others) come with "
    "NVIDIA's own terms, listed in the files it installs.")
