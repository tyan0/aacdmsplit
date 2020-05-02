# aacdmsplit
Lossless AAC splitter for dual mono ADTS
## What is this?
デュアルモノAAC ADTSファイルを二つのモノラルAACファイルに無劣化(再エンコード無し)で分離するツール。

## How to use
```aacdmsplit <file name>```

元ファイルと同じディレクトリに、元のファイル名に" SCE#0"と" SCE#1"を付加した2つのファイルが出来上がる。

## How to buid
作者はcygwinのmingwコンパイラを使用してビルドしています。
ビルドには[libfaad2](https://www.audiocoding.com/faad2.html)に同梱のパッチ(faad2.patch)を当てた必要です。別途ダウンロードしてビルドしてください。

### ビルド手順
* libfaad2にパッチ(faad2.patch)を当てる。
* libfaad2をビルドする。
* makeを実行

以上

## License
libfaad2をスタティックリンクしているので、[GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)とする。

## Special thanks
デュアルモノのSCEの切り出しについては、[Amatsukaze](https://github.com/nekopanda/Amatsukaze)のソースを参考にさせて頂いた。

AACファイルの読み込みについては、aaceditのソースを参考にさせて頂いた。
