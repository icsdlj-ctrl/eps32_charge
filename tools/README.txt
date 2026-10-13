普通固件编译直接使用 main/font.c，不需要此脚本。
新增中文字符时：安装 Pillow，下载 NotoSansSC 可变字体，
在工程目录运行 python tools/generate_font.py NotoSansSC.ttf，再编译固件。
字体与许可证：https://github.com/google/fonts/tree/main/ofl/notosanssc
