# aacdmsplit
Lossless AAC splitter for dual mono ATDS
## What is this?
デュアルモノAAC ATDSファイルを二つのモノラルAACファイルに無劣化(再エンコード無し)で分離するツール。

## How to use
```aacdmsplit <file name>```

元ファイルと同じディレクトリに、元のファイル名に" SCE#0"と" SCE#1"を付加した2つのファイルが出来上がる。

## How to buid
作者はcygwinのmingwコンパイラを使用してビルドしています。
ビルドにはlibfaad2が必要です。別途ダウンロードしてビルドしてください。

### ビルド手順
* libfaad2にパッチ(faad2.patch)を当てる。
* libfaad2をビルドする。
* makeを実行

以上

## License
libfaad2をスタティックリンクしているので、[GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)とする。
