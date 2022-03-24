<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ja_JP">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="171"/>
        <source>Error writing to storage</source>
        <translation>ストレージに書き込むのに失敗しました</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="197"/>
        <location filename="../downloadextractthread.cpp" line="386"/>
        <source>Error extracting archive: %1</source>
        <translation>アーカイブを展開するのに失敗しました</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="262"/>
        <source>Error mounting FAT32 partition</source>
        <translation>FAT32パーティションをマウントできませんでした</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="282"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>OSがFAT32パーティションをマウントしませんでした</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="305"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>カレントディレクトリを%1に変更できませんでした</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error running diskpart: %1</source>
        <translation>diskpartの実行に失敗しました: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="168"/>
        <source>Error removing existing partitions</source>
        <translation>既に有るパーティションを削除する際にエラーが発生しました。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="194"/>
        <source>Authentication cancelled</source>
        <translation>認証がキャンセルされました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="197"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>ディスク%1にアクセスするための権限を取得するためにauthopenを実行するのに失敗しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="198"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation type="unfinished">Raspberry Pi Imagerがリムーバブルボリュームへアクセスすることがプライバシー設定(「ファイルとフォルダー」の中、またはディスクへのすべてのアクセス権限を付与する)で許可されているかを確認してください。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="220"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>ストレージを開けませんでした。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="262"/>
        <source>discarding existing data on drive</source>
        <translation>ドライブの現存するすべてのデータを破棄します</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>ドライブの最初と最後のMBを削除しています</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="288"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>MBRを削除している際にエラーが発生しました。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="779"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>ストレージを読むのに失敗しました。SDカードが壊れている可能性があります。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="855"/>
        <source>Waiting for FAT partition to be mounted</source>
        <translation>FATパーティションがマウントされるのを待っています</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="941"/>
        <source>Error mounting FAT32 partition</source>
        <translation>FAT32パーティションをマウントする際にエラーが発生しました。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="963"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>OSがFAT32パーティションをマウントしませんでした。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="996"/>
        <source>Unable to customize. File &apos;%1&apos; does not exist.</source>
        <translation>カスタマイズできません。%1が存在しません。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1000"/>
        <source>Customizing image</source>
        <translation>イメージをカスタマイズしています</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1085"/>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation>FATパーティションにfirstrun.shを作成する際にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1037"/>
        <source>Error writing to config.txt on FAT partition</source>
        <translation>FATパーティションにconfig.txtを書き込むときにエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1102"/>
        <source>Error creating user-data cloudinit file on FAT partition</source>
        <translation>FATパーティションにCloud-initのuser-dataファイル名前を作成するときにエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1116"/>
        <source>Error creating network-config cloudinit file on FAT partition</source>
        <translation>FATパーティションにCloud-initのnetwork-configファイルを作成するときにエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1139"/>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation>FATパーティションにcmdline.txtを書き込む際にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="432"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>ディスクにファイルを書き込む際にアクセスが拒否されました。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="437"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>フォルダーへのアクセスが制限されています。許可されたアプリにrpi-imager.exeとfat32format.exeを入れてもう一度お試しください。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="444"/>
        <source>Error writing file to disk</source>
        <translation>ファイルをディスクに書き込んでいる際にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="463"/>
        <source>Error downloading: %1</source>
        <translation>%1をダウンロードする際エラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="686"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>ストレージへの書き込み中にエラーが発生しました (フラッシング中)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="693"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>ストレージへの書き込み中にエラーが発生しました（fsync中)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="674"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>ダウンロードに失敗しました。ハッシュ値が一致していません。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="114"/>
        <source>opening drive</source>
        <translation>デバイスを開いています</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="300"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>カードの最後のパートを0で書き込む際書き込みエラーが発生しました。カードが示している容量と実際のカードの容量が違う可能性があります。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="389"/>
        <source>starting download</source>
        <translation>ダウンロードを開始中</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <source>Error writing first block (partition table)</source>
        <translation>最初のブロック（パーティションテーブル）を書き込み中にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="798"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>確認中にエラーが発生しました。書き込んだはずのデータが実際にSDカードに記録されたデータと一致していません。</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>パーティショニングに失敗しました: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>fat32formatを開始中にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>fat32formatを実行中にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>新しいドライブレターを判断している際にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>不適切なデバイス: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>udisk2を介してフォーマットするのに失敗しました</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>sfdiskを開始中にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>パーティショニングが想定したFATパーティション %1を作りませんでした</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>mkfs.fatを開始中にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>mkfs.fatを実行中にエラーが発生しました: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>このプラットフォームではフォーマットできません。</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="257"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>ストレージの容量が足りません。少なくとも%1GBは必要です。</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="263"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>入力されたファイルは適切なディスクイメージファイルではありません。ファイルサイズの%1は512バイトの倍数ではありません。</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="445"/>
        <source>Downloading and writing image</source>
        <translation>イメージをダウンロードして書き込んでいます</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="578"/>
        <source>Select image</source>
        <translation>イメージを選ぶ</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="979"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Wifiのパスワードをシステムのキーチェーンから読み取って設定しますか？</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>イメージファイルを開いています</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>イメージファイルを開く際にエラーが発生しました</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>はい</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="120"/>
        <source>CONTINUE</source>
        <translation>続ける</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="130"/>
        <source>QUIT</source>
        <translation>やめる</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="79"/>
        <source>Advanced options</source>
        <translation>詳細な設定</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="96"/>
        <source>Image customization options</source>
        <translation>カスタマイズオプション</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="104"/>
        <source>for this session only</source>
        <translation>このセッションでのみ有効にする</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="105"/>
        <source>to always use</source>
        <translation>いつも使う設定にする</translation>
    </message>
    <message>
        <source>Disable overscan</source>
        <translation type="vanished">オーバースキャンを無効化する</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="119"/>
        <source>Set hostname:</source>
        <translation>ホスト名:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="138"/>
        <source>Enable SSH</source>
        <translation>SSHを有効化する</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="160"/>
        <source>Use password authentication</source>
        <translation>パスワード認証を使う</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="170"/>
        <source>Allow public-key authentication only</source>
        <translation>公開鍵認証のみを許可する</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="188"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>ユーザー%1のためのauthorized_keys</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="261"/>
        <source>Configure wireless LAN</source>
        <translation>Wi-Fiを設定する</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="280"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="234"/>
        <location filename="../OptionsPopup.qml" line="300"/>
        <source>Password:</source>
        <translation>パスワード:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="200"/>
        <source>Set username and password</source>
        <translation>ユーザー名とパスワードを設定する</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="219"/>
        <source>Username:</source>
        <translation>ユーザー名</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="295"/>
        <source>Hidden SSID</source>
        <translation>ステルスSSID</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="316"/>
        <source>Show password</source>
        <translation>パスワードを見る</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="321"/>
        <source>Wireless LAN country:</source>
        <translation>Wifiを使う国:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="332"/>
        <source>Set locale settings</source>
        <translation>ロケール設定をする</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="342"/>
        <source>Time zone:</source>
        <translation>タイムゾーン:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="352"/>
        <source>Keyboard layout:</source>
        <translation>キーボードレイアウト:</translation>
    </message>
    <message>
        <source>Skip first-run wizard</source>
        <translation type="vanished">最初のセットアップウィザートをスキップする</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="365"/>
        <source>Persistent settings</source>
        <translation>永続的な設定</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="373"/>
        <source>Play sound when finished</source>
        <translation>終わったときに音を鳴らす</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="377"/>
        <source>Eject media when finished</source>
        <translation>終わったときにメディアを取り出す</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="381"/>
        <source>Enable telemetry</source>
        <translation type="unfinished">テレメトリーを有効化する</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="394"/>
        <source>SAVE</source>
        <translation>保存</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation>SDカードリーダー</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="72"/>
        <source>Warning: advanced settings set</source>
        <translation>警告: 詳細な設定が設定されています</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="86"/>
        <source>Would you like to apply the image customization settings saved earlier?</source>
        <translation>カスタマイズを適用しますか？</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="95"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>いいえ、設定をクリアする</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="105"/>
        <source>YES</source>
        <translation>はい</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="115"/>
        <source>EDIT SETTINGS</source>
        <translation>設定を編集する</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="24"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="99"/>
        <location filename="../main.qml" line="399"/>
        <source>Operating System</source>
        <translation>OS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="111"/>
        <source>CHOOSE OS</source>
        <translation>OSを選ぶ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="123"/>
        <source>Select this button to change the operating system</source>
        <translation>OSを変更するにはこのボタンを押してください</translation>
    </message>
    <message>
        <location filename="../main.qml" line="135"/>
        <location filename="../main.qml" line="713"/>
        <source>Storage</source>
        <translation>ストレージ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="147"/>
        <location filename="../main.qml" line="1038"/>
        <source>CHOOSE STORAGE</source>
        <translation>ストレージを選ぶ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="173"/>
        <source>WRITE</source>
        <translation>書き込む</translation>
    </message>
    <message>
        <location filename="../main.qml" line="177"/>
        <source>Select this button to start writing the image</source>
        <translation>書き込みをスタートさせるにはこのボタンを押してください</translation>
    </message>
    <message>
        <location filename="../main.qml" line="218"/>
        <source>CANCEL WRITE</source>
        <translation>書き込みをキャンセル</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <location filename="../main.qml" line="965"/>
        <source>Cancelling...</source>
        <translation>キャンセル中です...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="229"/>
        <source>CANCEL VERIFY</source>
        <translation>確認をやめる</translation>
    </message>
    <message>
        <location filename="../main.qml" line="232"/>
        <location filename="../main.qml" line="988"/>
        <location filename="../main.qml" line="1057"/>
        <source>Finalizing...</source>
        <translation>最終処理をしています...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="247"/>
        <source>Select this button to access advanced settings</source>
        <translation>詳細な設定を変更するのならこのボタンを押してください</translation>
    </message>
    <message>
        <location filename="../main.qml" line="261"/>
        <source>Using custom repository: %1</source>
        <translation>カスタムレポジトリを使います: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="270"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>キーボードの操作: 次のボタンに移動する→Tabキー  ボタンを押す/選択する→Spaceキー  上に行く/下に行く→矢印キー（上下）</translation>
    </message>
    <message>
        <location filename="../main.qml" line="290"/>
        <source>Language: </source>
        <translation>言語: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="313"/>
        <source>Keyboard: </source>
        <translation>キーボード: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="505"/>
        <location filename="../main.qml" line="1022"/>
        <source>Erase</source>
        <translation>削除</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Format card as FAT32</source>
        <translation>カードをFAT32でフォーマットする</translation>
    </message>
    <message>
        <location filename="../main.qml" line="515"/>
        <source>Use custom</source>
        <translation>カスタムイメージを使う</translation>
    </message>
    <message>
        <location filename="../main.qml" line="516"/>
        <source>Select a custom .img from your computer</source>
        <translation>自分で用意したイメージファイルを使う</translation>
    </message>
    <message>
        <location filename="../main.qml" line="461"/>
        <source>Back</source>
        <translation>戻る</translation>
    </message>
    <message>
        <location filename="../main.qml" line="157"/>
        <source>Select this button to change the destination storage device</source>
        <translation>書き込み先のストレージデバイスを選択するにはこのボタンを押してください</translation>
    </message>
    <message>
        <location filename="../main.qml" line="462"/>
        <source>Go back to main menu</source>
        <translation>メインメニューへ戻る</translation>
    </message>
    <message>
        <location filename="../main.qml" line="628"/>
        <source>Released: %1</source>
        <translation>リリース日時: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="638"/>
        <source>Cached on your computer</source>
        <translation>コンピュータにキャッシュされたファイル</translation>
    </message>
    <message>
        <location filename="../main.qml" line="640"/>
        <source>Local file</source>
        <translation>ローカルファイル</translation>
    </message>
    <message>
        <location filename="../main.qml" line="641"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">インターネットからダウンロードする (%1GB)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="766"/>
        <location filename="../main.qml" line="818"/>
        <location filename="../main.qml" line="824"/>
        <source>Mounted as %1</source>
        <translation>%1としてマウントされています</translation>
    </message>
    <message>
        <location filename="../main.qml" line="820"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[書き込み禁止]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="862"/>
        <source>Are you sure you want to quit?</source>
        <translation>本当にやめますか？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="863"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imagerは現在まだ処理中です。&lt;bt&gt;本当にやめますか？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="874"/>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <location filename="../main.qml" line="882"/>
        <source>Preparing to write...</source>
        <translation>書き込み準備中...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="906"/>
        <source>Update available</source>
        <translation>アップデートがあります</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>新しいバージョンのImagerがあります。&lt;br&gt;ダウンロードするためにウェブサイトに行きますか？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="968"/>
        <source>Writing... %1%</source>
        <translation>書き込み中... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="895"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>%1に存在するすべてのデータは完全に削除されます。本当に続けますか？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="947"/>
        <source>Error downloading OS list from Internet</source>
        <translation>OSのリストをダウンロードする際にエラーが発生しました。</translation>
    </message>
    <message>
        <location filename="../main.qml" line="991"/>
        <source>Verifying... %1%</source>
        <translation>確認中... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="998"/>
        <source>Preparing to write... (%1)</source>
        <translation>書き込み準備中... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1014"/>
        <source>Error</source>
        <translation>エラー</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1021"/>
        <source>Write Successful</source>
        <translation>書き込みが正常に終了しました</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1023"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b%gt;%1&lt;/b&gt; は削除されました。&lt;br&gt;&lt;bt&gt;SDカードをSDカードリーダーから取り出しても良いです。</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;</source>
        <translation type="vanished">&lt;b&gt;%1&lt;/b&gt; は&lt;b&gt;%2&lt;/b&gt;に書き込まれました。</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1030"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; は&lt;b&gt;%2&lt;/b&gt;に書き込まれました。&lt;br&gt;&lt;br&gt;SDカードをSDカードリーダーから取り出しても良いです。</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1098"/>
        <source>Error parsing os_list.json</source>
        <translation>os_list.jsonの処理中にエラーが発生しました</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1271"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>最初にイメージファイルがあるUSBメモリを接続してください。&lt;br&gt;イメージファイルはUSBメモリの一番上（ルートフォルダー）に入れてください。</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1287"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>SDカードは書き込みが制限されています。&lt;br&gt;カードの左上にあるロックスイッチを上げてロックを解除し、もう一度お試しください。</translation>
    </message>
</context>
</TS>
