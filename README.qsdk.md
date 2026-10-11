# QSDK iwinfo バックエンド

QSDK の `wifi*` ラジオと `ath*` VAP を扱う `qcawificfg80211` バックエンドを追加しています。通常の情報取得には nl80211 を使い、QSDK 固有の情報を sysfs と hostapd の設定ファイルから取得します。スキャンには純正ドライバーの WEXT インターフェースを使います。

6 GHz のチャンネル変換、QSDK の VAP ごとの hostapd ファイル、HE/EHT と 320 MHz の機能情報、SAE/OWE の表示に対応しています。既存の `iwinfo_ops` と一覧データの構造体レイアウトは維持しています。

`BACKENDS='nl80211 qcawificfg80211'` でビルドしてください。`tests/qsdk-selfcheck.c` でインターフェース名の検証、HE/EHT の機能情報の解析、QSDK の転送速度の単位変換を確認できます。

WN-7T94XR v2.5.1 用のビルド手順と IPK は [LuCI フォーク](https://github.com/onokatio/luci/tree/qsdk-wn7t94xr/contrib/wn7t94xr)にあります。本体のドライバーは変更しません。接続クライアントの EHT レート情報や、他機種での動作は未検証です。
