# aacdmsplit
Lossless AAC splitter for dual mono ADTS
## What is this?
デュアルモノAAC ADTSファイルを二つのモノラルAAC ADTSファイルに無劣化(再エンコード無し)で分離するツール。

## Download
[こちら](https://github.com/tyan0/aacdmsplit/releases)からどうぞ。

## How to use
```aacdmsplit <file name>```

元ファイルと同じディレクトリに、元のファイル名に" SCE0"と" SCE1"を付加した2つのファイルが出来上がります。

## How to buid
作者はcygwinのmingwコンパイラを使用してビルドしています。
ビルドには、同梱のパッチ(faad2.patch)を当てた[libfaad2](https://sourceforge.net/projects/faac/files/faad2-src/faad2-2.8.0/) (2.8.8)が必要です。
別途ダウンロードしてビルドしてください。

### ビルド手順
* libfaad2にパッチ(faad2.patch)を当てる。
* libfaad2をビルドする。
* makeを実行

以上

小変更でLinux上でもビルドできるはずです。具体的には、
* Makefile中、CXXの定義を変更
* aacdmsplit.ccに ```#include <linux/limits.h>``` を追加

ぐらいでしょうか。

## License
libfaad2をスタティックリンクしているので、[GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)とします。

## Special thanks
デュアルモノのSCEの切り出しについては、[Amatsukaze](https://github.com/nekopanda/Amatsukaze)のソースを参考にさせて頂きました。

AAC ADTSファイルの読み込みについては、aaceditのソースを参考にさせて頂きました。
