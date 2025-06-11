<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ja_JP">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>アーカイブを展開するのに失敗しました</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>FAT32パーティションをマウントできませんでした</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>OSがFAT32パーティションをマウントしませんでした</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>カレントディレクトリを%1に変更できませんでした</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">ストレージを読むのに失敗しました。SDカードが壊れている可能性があります。</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">確認中にエラーが発生しました。書き込んだはずのデータが実際にSDカードに記録されたデータと一致していません。</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>ドライブをアンマウントしています</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>デバイスを開いています</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>diskpartの実行に失敗しました: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>既に有るパーティションを削除する際にエラーが発生しました。</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>認証がキャンセルされました</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>ディスク%1にアクセスするための権限を取得するためにauthopenを実行するのに失敗しました</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Raspberry Pi Imagerがリムーバブルボリュームへアクセスすることが「プライバシーとセキュリティ」の「ファイルとフォルダー」の設定、または「フルディスクアクセス」の付与によって許可されているかを確認してください。</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>ストレージを開けませんでした。</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>ドライブの現存するすべてのデータを破棄します</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>ドライブの最初と最後のMBを削除しています</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>MBRを削除している際にエラーが発生しました。</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>カードの最後のパートを0で書き込む際書き込みエラーが発生しました。カードが示している容量と実際のカードの容量が違う可能性があります。</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>ダウンロードを開始中</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>%1をダウンロードする際エラーが発生しました</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>ディスクにファイルを書き込む際にアクセスが拒否されました。</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>フォルダーへのアクセスが制限されています。許可されたアプリにrpi-imager.exeとfat32format.exeを入れてもう一度お試しください。</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>ファイルをディスクに書き込んでいる際にエラーが発生しました</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>ストレージへの書き込み中にエラーが発生しました (フラッシング中)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>ストレージへの書き込み中にエラーが発生しました（fsync中)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>最初のブロック（パーティションテーブル）を書き込み中にエラーが発生しました</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>ストレージを読むのに失敗しました。SDカードが壊れている可能性があります。</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>確認中にエラーが発生しました。書き込んだはずのデータが実際にSDカードに記録されたデータと一致していません。</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>イメージをカスタマイズしています</translation>
    </message>
    <message>
        <source>Cached file is corrupt. SHA256 hash does not match expected value.&lt;br&gt;The cache file will be removed and the download will restart.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Local file is corrupt or has incorrect SHA256 hash.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Download appears to be corrupt. SHA256 hash does not match.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2&lt;br&gt;Please check your network connection and try again.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error partitioning: %1</source>
        <translation>パーティショニングに失敗しました: %1</translation>
    </message>
    <message>
        <source>Error starting fat32format</source>
        <translation>fat32formatを開始中にエラーが発生しました</translation>
    </message>
    <message>
        <source>Error running fat32format: %1</source>
        <translation>fat32formatを実行中にエラーが発生しました</translation>
    </message>
    <message>
        <source>Error determining new drive letter</source>
        <translation>新しいドライブレターを判断している際にエラーが発生しました</translation>
    </message>
    <message>
        <source>Invalid device: %1</source>
        <translation>不適切なデバイス: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>udisk2を介してフォーマットするのに失敗しました</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>このプラットフォームではフォーマットできません。</translation>
    </message>
    <message>
        <source>Error opening device for formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error writing to device during formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error seeking on device during formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Invalid parameters for formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Insufficient space on device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Unknown formatting error</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation type="unfinished">ストレージ</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Exclude System Drives</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>gigabytes</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation type="unfinished">%1 としてマウントされています</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[書き込み禁止]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SDカードへの書き込みが制限されています。&lt;br&gt;カードの左上にあるロックスイッチを上げてロックを解除し、もう一度お試しください。</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">デバイスを選択</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Raspberry Piデバイス</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>ストレージの容量が足りません。少なくとも%1GBは必要です。</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>入力されたファイルは適切なディスクイメージファイルではありません。ファイルサイズの%1は512バイトの倍数ではありません。</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>イメージをダウンロードして書き込んでいます</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>イメージを選ぶ</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Wifiのパスワードをシステムのキーチェーンから読み取って設定しますか？</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>イメージファイルを開いています</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>イメージファイルを開く際にエラーが発生しました</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <source>NO</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>はい</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>続ける</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>やめる</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <source>Recommended</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <source>Operating System</source>
        <translation type="unfinished">OS</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">戻る</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">メインメニューへ戻る</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">リリース日時: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">コンピュータにキャッシュされたファイル</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">ローカルファイル</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">インターネットからダウンロード - %1 GB</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">最初にイメージファイルがあるUSBメモリを接続してください。&lt;br&gt;イメージファイルはUSBメモリの一番上（ルートフォルダー）に入れてください。</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">ホスト名:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">ユーザー名とパスワードを設定する</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">ユーザー名</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">パスワード:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Wi-Fiを設定する</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">ステルスSSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Wifiを使う国:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">ロケール設定をする</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">タイムゾーン:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">キーボードレイアウト:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">終わったときに音を鳴らす</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">終わったときにメディアを取り出す</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">テレメトリーを有効化</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>サービス</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>オプション</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>保存</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">SSHを有効化する</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">パスワード認証を使う</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">公開鍵認証のみを許可する</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">ユーザー%1のためのauthorized_keys</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">ssh-keygenを実行する</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>NO</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>いいえ、設定をクリアする</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>はい</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>設定を編集する</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Raspberry Piデバイス</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>対象のRaspberry Piを選択するには、このボタンを押してください。</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>OS</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>OSを選択</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>OSを変更するにはこのボタンを押してください</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>ストレージ</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>ストレージを選択</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>書き込み先のストレージデバイスを選択するにはこのボタンを押してください</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>書き込みをキャンセル</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>キャンセル中です...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>確認をやめる</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>最終処理をしています...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>次へ</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>書き込みをスタートさせるにはこのボタンを押してください</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>カスタムレポジトリを使います: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>キーボードの操作: 次のボタンに移動する→Tabキー  ボタンを押す/選択する→Spaceキー  上に行く/下に行く→矢印キー（上下）</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>言語: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>キーボード: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>本当にやめますか？</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imagerは現在まだ処理中です。&lt;bt&gt;本当にやめますか？</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>書き込み準備中...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>%1 に存在するすべてのデータは完全に削除されます。本当に続けますか？</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>アップデートがあります</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>新しいバージョンのImagerがあります。&lt;br&gt;ダウンロードするためにウェブサイトを開きますか？</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>書き込み中... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>確認中... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>書き込み準備中... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>エラー</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>書き込みが正常に終了しました</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>削除</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b%gt;%1&lt;/b&gt; は削除されました。&lt;br&gt;&lt;bt&gt;SDカードをSDカードリーダーから取り出すことができます。</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; は&lt;b&gt;%2&lt;/b&gt;に書き込まれました。&lt;br&gt;&lt;br&gt;SDカードをSDカードリーダーから取り出すことができます。</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>カードをFAT32でフォーマットする</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>カスタムイメージを使う</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>自分で用意したイメージファイルを使う</translation>
    </message>
    <message>
        <source>SKIP CACHE VERIFICATION</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Starting download...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Verifying cached file... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Verifying cached file...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Starting write...</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>
