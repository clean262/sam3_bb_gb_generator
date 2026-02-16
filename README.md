<p align="center">
  <a href="https://www.nicovideo.jp/watch/sm45931905" target="_blank" rel="noopener noreferrer">
    <img src="./assets/gif/click.gif" alt="demo click" width="45%">
  </a>
  <a href="https://www.nicovideo.jp/watch/sm45931905" target="_blank" rel="noopener noreferrer">
  <img src="./assets/gif/text.gif" alt="demo text" width="45%">
  </a>
</p>

# はじめに
[![Release](https://img.shields.io/github/v/release/clean262/sam3_bb_gb_generator)](https://github.com/clean262/sam3_bb_gb_generator)
[![Downloads](https://img.shields.io/github/downloads/clean262/sam3_bb_gb_generator/total)](https://github.com/clean262/sam3_bb_gb_generator/releases/latest)
[![License](https://img.shields.io/github/license/clean262/sam3_bb_gb_generator)](https://github.com/clean262/sam3_bb_gb_generator/blob/main/LICENSE)
[![Last Commit](https://img.shields.io/github/last-commit/clean262/sam3_bb_gb_generator)](https://github.com/clean262/sam3_bb_gb_generator/commits/main)

クリックで簡単に動画を切り取れるAviutl2プラグインです。

- **マウスクリック**だけで前景・背景を指定
- 切り抜いた前景のみ(背景のみ)を**Aviutl2に自動挿入**
- 挿入方法は**BB/GB/透過と選択可能**
- プレビューしながら前景・背景を調整
- 複数フレームに対してクリック可能
- point/Box形式で前景・背景を指定可能
- テキストで前景を指定可能(実装中)

**紹介動画**は[こちら](https://www.nicovideo.jp/watch/sm45931905)から<br>
<a href="https://www.nicovideo.jp/watch/sm45931905">
  <img src="assets/samune.png?raw=true" alt="samune" height="320">
</a>

# 利用に際して
本プラグインを用いて動画制作を行った場合[親作品登録](https://www.nicovideo.jp/watch/sm45931905)をしていただけると開発の励みになります。<br>
親作品登録いただいたら見に行きます。

開発状況等は作者[X: 旧twitter](https://x.com/clean123525)を参照してください。

バグ報告や機能追加の要望、利用に際して分からない点がありましたら[Issues](https://github.com/clean262/sam3_bb_gb_generator/issues)から気軽にお願いします。<br>
右上`New issue`ボタンから送れます。

また本プラグインはコミュニティの発展のために、Aviutl2本体の[親作品登録](https://commons.nicovideo.jp/works/nc422952)を推奨しています。<br>
[Aviutl2-catalog](https://github.com/Neosku/aviutl2-catalog)を利用することでAviutl2本体/プラグイン/スクリプトの親作品リストを一括で取得できます。<br>
<a href="https://commons.nicovideo.jp/works/nc422952">
  <img src="assets/aviutl2.png?raw=true" alt="samune" height="320">
</a>

# 関連プラグイン/ソフト
### 1. Aviutl2 かんたん物体切り抜きプラグイン<br>
<!-- [Github](https://github.com/clean262/sam_frame_export_filter) [解説動画](https://www.nicovideo.jp/watch/sm45723074)<br> -->
[![GitHub](https://img.shields.io/badge/GitHub-Repo-181717?logo=github&logoColor=white)](https://github.com/clean262/sam_frame_export_filter)
[![解説動画](https://img.shields.io/badge/ニコニコ動画-解説-252525?logo=niconico&logoColor=white)](https://www.nicovideo.jp/watch/sm45723074) <br> 
本プラグインの静止画切り抜き版です。<br>
本プラグインと比較して軽量のため、スペックが低いパソコンでも動かすことが出来ます。<br>
<a href="https://www.nicovideo.jp/watch/sm45723074">
  <img src="assets/related/aviutl2_sam.jpg?raw=true" alt="samune" height="320">
</a>

### 2. 静止画背景を自動で切り抜こう - さぶんぬっころ
[![X](https://img.shields.io/badge/Downloads-000000?logo=x&logoColor=white)](https://x.com/was9th/status/1993200961456886129)
[![解説動画](https://img.shields.io/badge/ニコニコ動画-解説-252525?logo=niconico&logoColor=white)](https://www.nicovideo.jp/watch/sm45665649) <br>
Aviutl2プラグインではありませんが、背景素材のみのシーンが存在する場合において前景を綺麗に切り取ることが出来ます。<br>
<a href="https://www.nicovideo.jp/watch/sm45665649">
  <img src="assets/related/sabun.jpg?raw=true" alt="samune" height="320">
</a>

### 3. AIを使って簡単にBB素材を作ろう！
[![Open in Colab](https://img.shields.io/badge/Open%20in-Colab-F9AB00?logo=googlecolab&logoColor=white)](https://colab.research.google.com/drive/14cdEQP3l5t0Ma5OyxptBnNCokOi6LBIw?usp=sharing)
[![解説動画](https://img.shields.io/badge/ニコニコ動画-解説-252525?logo=niconico&logoColor=white)](https://www.nicovideo.jp/watch/sm44970717) <br>
本プラグインが採用しているSAM3の1世代前のモデルであるSAM2で切り抜きが行えます。<br>
Aviutl2プラグインではありませんが、Google Colabで実行できるため導入不要で切り抜きが行えます。<br>
<a href="https://www.nicovideo.jp/watch/sm44970717">
  <img src="assets/related/bb_sam2.jpeg?raw=true" alt="samune" height="320">
</a>

### 4. Aviutl2 Remove Background
[![GitHub](https://img.shields.io/badge/GitHub-Repo-181717?logo=github&logoColor=white)](https://github.com/arajun94/AviUtl2_Remove_Background)<br>
本プラグインのSAM2版です。
性能は低下しますが、複数のモデルを選択できます。

# 実行確認環境
- Aviutl2 beta32以上(**必須**)
- Windows11
- NVIDIA Geforece RTX 3060

# 導入方法
## Nvidia driver install
NVIDIAのGPUを搭載したパソコンを所有している方は実行してください。搭載していない方は飛ばしてください。<br>
GPUを用いて高速な動画切り取りを行うことが出来ます。<br>
この[ページ](https://www.kkaneko.jp/tools/win/nvidiadriver.html)に従ってインストールしてください。<br>

```bash
nvidia-smi
```
上記コマンドをターミナル等のアプリで入力した際に、CUDA Version: 12.6 以上の表記が出ていればOKです。<br>
**既にNvidia driverをインストールされている方でもCUDA Version: 12.6 以上の表記が出るかは確認してください**

## gitのインストール
ターミナル等のアプリで以下のコマンドを入力し、gitがインストール済みと確認できる方は飛ばしてください
```bash
git --version
```
エラーが出る方は元々インストールされてませんので、この[ページ](https://prog-8.com/docs/git-env-win)に従ってgitをインストールしてください。<br>
サイト内での`2. Gitの初期設定`まで実行していただければOKです。

## SAM3の利用登録
[Hugging Face](https://huggingface.co/join)にアクセスし、アカウントを作成してください。<br>
<img src="assets/sign_in.png?raw=true" alt="sign in" height="320">
<img src="assets/sign_in2.png?raw=true" alt="sign in2" height="320">

[facebook/sam3](https://huggingface.co/facebook/sam3)にアクセスし、以下の項目を入力してください。<br>
許可が下りるまでしばらく時間がかかります(数十分から数時間)<br>
<img src="assets/terms.png?raw=true" alt="term" height="320">

access tokenを[発行](https://huggingface.co/settings/tokens)し
右上のCreate new tokenから、新しいtokenを作成してください。<br>
ここで発行したtokenは一度しか表示されないので、保存し忘れた場合新たなtokenを作成してください。<br>
<img src="assets/token.png?raw=true" alt="setting" height="320">

すぐ下に記載のある[プラグインのインストール](##プラグインのインストール)からプラグインをインストール後(catalogからでもReleaseからでもどちらからでも大丈夫です)、<br>
取得したトークンをターミナル等のアプリで、以下のコマンドを**1行ずつ**入力してください。<br>
トークンを入力するタイミングは`Enter your token(input will not be visible):`の時です。<br>
トークンはパスワードと同様、他人に見せてはならないものなので、入力しても何も表示されませんが入力されています。<br>
Add token as git credential?にはnでOKです。<br>
```bash
cd C:\ProgramData\aviutl2\Plugin\SAM3\python
.\uv.exe run hf auth login
```
トークンを登録できたかを確認するには以下のコマンドを1行ずつ入力してください。
```bash
cd C:\ProgramData\aviutl2\Plugin\SAM3\python
.\uv.exe run python -c "from huggingface_hub import whoami; print(whoami())"
```
`{'type': 'user', 'id':`...のようになればOKです。

SAM3の利用申請は、SAM3側が手動で許諾をとっているようです。<br>
[こちら](https://huggingface.co/settings/gated-repo)でSAM3がAcceptedになっていれば承認されています。Pendingの場合、まだ保留中です。しばらくお待ちください。

## プラグインのインストール
Nvidia driverのインストール、gitのインストールとSAM3の利用登録を終えたら、プラグインのインストールには2種類のインストール方法があります。

### Aviutl2-catalogからインストールする場合(**推奨**)
[Aviutl2-catalog](https://github.com/Neosku/aviutl2-catalog)をダウンロードし、インストールボタンを押すだけで導入は完了です。

> **Aviutl2-catalog** は、AviUtl2 のプラグインやスクリプトを「検索→導入→更新」まで一括管理できるデスクトップアプリです。<br>
> 従来のようにダウンロード時には解凍してファイルを移動させる必要はありません。また、アップデートがあれば通知が来て1クリックで更新も完了します。<br>
> 利用したプラグイン/スクリプトの親作品リストも一括で取得できます。

<a href="https://commons.nicovideo.jp/works/nc422952">
  <img src="assets/catalog.png?raw=true" alt="samune" height="320">
</a>

### Releaseからインストールする場合(optional)
catalogを利用していない人はこちらの方法でインストールしてください。
[Github Release](https://github.com/clean262/sam3_bb_gb_generator/releases)から最新の`SAM3_win64.zip`をダウンロードして下さい。<br>
zipを解凍後、`install.cmd`をダブルクリックしてください。セキュリティの警告に対して実行を押していただくと、インストールが完了します。


# 使い方
Aviutl2起動後、左上 `表示` の欄から `SAM3` を選択してください。<br>
<img src="assets/how_to_use/display.png?raw=true" alt="term" height="200">

下のような画面が表示されるので、タイムライン上で切り抜きたい動画を選択し、挿入形式を決定してください。<br>
<img src="assets/how_to_use/main_ui.png?raw=true" alt="term" height="200">

実行ボタンを押し、Gradio is runningの表示が出たら切り抜き画面を開くを押してください。<br>
<img src="assets/how_to_use/gradio_is_running.png?raw=true" alt="term" height="200">

切り抜き用の画面が開かれます。<br>
<img src="assets/how_to_use/gradio_main.png?raw=true" alt="term" height="640">

画面をクリックすると選択範囲が示されます。<br>
間違えた場合は右のReset Promptボタンを押してください。<br>
Frameのバーを動かすと、複数フレームで選択したい領域を指定できます。<br>
また、`point label`を`negative`にすると選択したくない領域を指摘できます <br>
<img src="assets/how_to_use/gradio_click.png?raw=true" alt="term" height="640">

Prompt typeをBoxに指定し、左上の点と右下の点を指定すると長方形で範囲を指定できます。<br>
Boxで指定した後、クリックで追加指定することも可能です。(逆は出来ません)
<img src="assets/how_to_use/gradio_box.png?raw=true" alt="term" height="640">

Object IDを指定することで、複数の物体を指定することも可能です。<br>
<img src="assets/how_to_use/gradio_multi.png?raw=true" alt="term" height="640">

指定したら`Propagate across segments`ボタンを押してください。切り抜きが行われます。<br>
切り抜き精度に不満がある場合はFrameのバーを動かし、再度クリックやBoxで領域指定してから`Propagate across segments`ボタンを押すことも可能です。<br>
<img src="assets/how_to_use/gradio_propagate.png?raw=true" alt="term" height="640">

`Render Preview`ボタンを押すと切り抜き結果を確認できます。<br>
<img src="assets/how_to_use/gradio_preview.png?raw=true" alt="term" height="640">

最後に、`Finish`を押すとAviutl2に切り抜き結果が自動挿入されます。<br>
<img src="assets/how_to_use/result.png?raw=true" alt="term" height="640">

## ライセンス

**MIT ライセンス** 

<!-- # uv install
```bash
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
uv --version  # powershell再起動後表示が出ればOK
``` -->

<!-- # file install
```bash
git clone 
uv sync
uv run hf auth login # access tokenを発行(https://huggingface.co/settings/tokens)し入力(絶対公開しないでください) Add token as git credential?にはnでOK
uv run python -c "from huggingface_hub import whoami; print(whoami())" # access_tokenが出るならOK
uv run python -c "import torch; print('cuda:', torch.cuda.is_available())" # cuda: TrueならOK
uv run python app.py
``` -->