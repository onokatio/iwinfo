# QSDK iwinfo バックエンド

この `qsdk-19.07` ブランチは OpenWrt 19.07 系の iwinfo を基にした旧系統です。現行の LuCI 25.12 構成で使う iwinfo は、既定ブランチの [`qsdk-25.12`](https://github.com/onokatio/iwinfo/tree/qsdk-25.12) で管理しています。

QSDK の `wifi*` ラジオと `ath*` VAP を扱う `qcawificfg80211` バックエンドを追加しています。通常の情報取得には nl80211 を使い、QSDK 固有の情報を sysfs と hostapd の設定ファイルから取得します。スキャンには純正ドライバーの WEXT インターフェースを使います。

6 GHz のチャンネル変換、QSDK の VAP ごとの hostapd ファイル、HE/EHT と 320 MHz の機能情報、SAE/OWE の表示に対応しています。既存の `iwinfo_ops` と一覧データの構造体レイアウトは維持しています。

接続端末の一覧がnl80211で空になる場合は、本体の `/usr/sbin/wlanconfig <VAP> list sta` からMACアドレス・信号強度・送受信速度を取得します。これらを既存のiwinfo形式で返すため、LuCIの標準Associated Stationsに表示できます。取得に失敗した場合は端末情報を返しません。

`BACKENDS='nl80211 qcawificfg80211'` でビルドしてください。`tests/qsdk-selfcheck.c` でインターフェース名の検証、HE/EHT の機能情報の解析、QSDK の転送速度の単位変換、接続端末一覧の取得と不正な入力・コマンド失敗時の処理を確認できます。

WN-7T94XR v2.5.1 用の旧版のビルド手順と IPK は [LuCI フォークの qsdk-19.07](https://github.com/onokatio/luci/tree/qsdk-19.07/contrib/wn7t94xr)にあります。本体のドライバーは変更しません。接続クライアントの EHT レート情報や、他機種での動作は未検証です。
