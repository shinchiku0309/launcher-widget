# Launcher Widget

Launcher Widget は、Windows デスクトップ上で常駐利用できる軽量なランチャーアプリです。C++ と Win32 API を中心に実装しており、アプリ、ファイル、フォルダー、URL、Windows 設定、キー入力、メディア操作などをボタンに割り当てて呼び出せます。

## 特徴

- C++ / Win32 API ベースの軽量な Windows ネイティブアプリ
- 行数、列数、ボタンサイズ、余白、ページ構成を変更できるグリッド型ランチャー
- 複数ページの追加、削除、名称変更、表示順変更に対応
- ボタンごとにタイトル、表示テキスト、画像を設定可能
- アプリ、ファイル、フォルダー、URL、Windows 設定を起動可能
- 106 / 101 配列を選べるキー選択 UI と、同時押下・順次押下のキー入力に対応
- 音量、ミュート、再生/一時停止、停止、次のトラック、前のトラックなどのメディア操作に対応
- アプリ割り当て時のアイコン取得、キー割り当て時のキーキャップ風表示に対応
- 常に手前に表示、タスクトレイ表示、Windows 起動時の自動実行を設定可能

## 動作環境

- Windows 10 以降
- Visual Studio 2022 Build Tools、C++ デスクトップ開発環境
- インストーラー作成時のみ Inno Setup 6

## ビルド

Developer PowerShell など、MSBuild を実行できる環境で以下を実行します。

```powershell
msbuild Launcher.sln /p:Configuration=Release /p:Platform=x64
```

ビルド後の実行ファイルは以下に作成されます。

```text
bin/Release/Launcher.exe
```

## インストーラー作成

Release ビルド後、Inno Setup で以下を実行します。

```powershell
ISCC.exe installer.iss
```

インストーラーは以下に作成されます。

```text
bin/Installer/LauncherSetup.exe
```

## 基本操作

- ボタンを左クリックすると、割り当てた機能を実行します。
- ボタンを右クリックすると、機能、タイトル、表示テキスト、画像などを編集できます。
- ページタイトルを右クリックすると、ページの追加や削除を実行できます。
- Page Setting 画面では、ページの追加、削除、名称変更、表示順変更をまとめて設定できます。
- Settings 画面では、レイアウト、キーボード配列、常に手前に表示、タスクトレイ表示、自動起動などを設定できます。

## 主なファイル

- [src/main.cpp](./src/main.cpp): アプリ本体のソースコード
- [Launcher.vcxproj](./Launcher.vcxproj): Visual Studio C++ プロジェクト
- [Launcher.sln](./Launcher.sln): Visual Studio ソリューション
- [installer.iss](./installer.iss): Inno Setup 用インストーラー定義
- [app.ico](./app.ico): アプリケーションアイコン

## ライセンス

このプロジェクトは MIT License で公開しています。詳細は [LICENSE](./LICENSE) を参照してください。
