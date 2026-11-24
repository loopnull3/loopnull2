# astcenc

The decoder and encoder sources of [astc-encoder](https://github.com/ARM-software/astc-encoder) (Arm, Apache-2.0: `LICENSE`), as the PyPI source
distribution astc-encoder-py 0.1.12 bundles them, unchanged. `tools/android/build_bananative_hosts.sh` copies them into the Banana Native checkout
as `dm/third_party/astcenc`, where the hosts' HDR block conversion (`0001-astc-error-colour-blocks.patch`, `dm/gfx/vk_android/vka_astc_hdr.cpp`) uses them.
