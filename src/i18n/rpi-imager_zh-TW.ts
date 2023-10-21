<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_TW">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="196"/>
        <location filename="../downloadextractthread.cpp" line="385"/>
        <source>Error extracting archive: %1</source>
        <translation>解壓縮檔案時發生錯誤：%1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="261"/>
        <source>Error mounting FAT32 partition</source>
        <translation>掛載 FAT32 分割區時發生錯誤</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="281"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>作業系統未掛載 FAT32 分割區</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="304"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>變更至目錄 &apos;%1&apos; 時發生錯誤</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="118"/>
        <source>unmounting drive</source>
        <translation>正在卸載磁碟</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="138"/>
        <source>opening drive</source>
        <translation>正在開啟磁碟</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="166"/>
        <source>Error running diskpart: %1</source>
        <translation>執行 diskpart 時發生錯誤：%1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="187"/>
        <source>Error removing existing partitions</source>
        <translation>移除現有分割區時發生錯誤</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="213"/>
        <source>Authentication cancelled</source>
        <translation>已取消驗證</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="216"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>執行 authopen 以獲取磁碟裝置 &apos;%1&apos; 存取權限時發生錯誤</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>請確認在隱私設定中是否允許 &apos;Raspberry Pi Imager&apos; 存取 &apos;可移除磁碟&apos;（在 &apos;檔案和資料夾&apos; 下，或者給予它 &apos;完全磁碟存取權&apos;）。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="239"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>無法開啟儲存裝置 &apos;%1&apos;。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="281"/>
        <source>discarding existing data on drive</source>
        <translation>正在丟棄磁碟上的現有資料</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="301"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>正在將磁碟的第一個和最後一個 MB 清除</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="307"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>在將 MBR 清除時寫入錯誤</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="319"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>嘗試將卡的最後部分清除時寫入錯誤。&lt;br&gt;卡可能在廣告錯誤的容量（可能是偽造的）。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="408"/>
        <source>starting download</source>
        <translation>開始下載</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="466"/>
        <source>Error downloading: %1</source>
        <translation>下載時發生錯誤：%1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="663"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>寫入檔案至磁碟時存取被拒絕的錯誤。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="668"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>似乎已啟用受控資料夾存取。請將 rpi-imager.exe 和 fat32format.exe 兩者都加入允許的應用程式清單，然後再試一次。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="675"/>
        <source>Error writing file to disk</source>
        <translation>寫入檔案至磁碟時發生錯誤</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="697"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>下載損壞。雜湊值不符</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="709"/>
        <location filename="../downloadthread.cpp" line="761"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>寫入儲存裝置時發生錯誤（在清除時）</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="716"/>
        <location filename="../downloadthread.cpp" line="768"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>寫入儲存裝置時發生錯誤（在 fsync 時）</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="751"/>
        <source>Error writing first block (partition table)</source>
        <translation>寫入第一個區塊（分割表）時發生錯誤</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="826"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>從儲存裝置讀取時發生錯誤。&lt;br&gt;SD 卡可能已損壞。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="845"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>驗證寫入失敗。SD 卡的內容與寫入的內容不一致。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="898"/>
        <source>Customizing image</source>
        <translation>自訂映像檔</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>分割時發生錯誤：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>開始執行 fat32format 時發生錯誤</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>執行 fat32format 時發生錯誤：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>確定新磁碟機代號時發生錯誤</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>無效裝置：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>格式化時發生錯誤（透過 udisks2）</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>開始執行 sfdisk 時發生錯誤</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>並未建立預期的 FAT 分割區 %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>開始執行 mkfs.fat 時發生錯誤</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>執行 mkfs.fat 時發生錯誤：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>此平台未實作格式化</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="248"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>儲存容量不足。&lt;br&gt;至少需要 %1 GB。</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="254"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>輸入檔案不是有效的磁碟映像檔。&lt;br&gt;檔案大小 %1 位元組不是 512 位元組的倍數。</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="442"/>
        <source>Downloading and writing image</source>
        <translation>正在下載並寫入映像檔</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="575"/>
        <source>Select image</source>
        <translation>選擇映像檔</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="896"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>您是否想要從系統鑰匙圈預填 Wi-Fi 密碼？</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>正在開啟映像檔</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>開啟映像檔時發生錯誤</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>否</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>是</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="120"/>
        <source>CONTINUE</source>
        <translation>繼續</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="130"/>
        <source>QUIT</source>
        <translation>結束</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="20"/>
        <source>OS Customization</source>
        <translation>作業系統客製化</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="52"/>
        <source>OS customization options</source>
        <translation>作業系統客製化選項</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="57"/>
        <source>for this session only</source>
        <translation>僅此工作階段</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="58"/>
        <source>to always use</source>
        <translation>總是使用</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="71"/>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="79"/>
        <source>Services</source>
        <translation>服務</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="82"/>
        <source>Options</source>
        <translation>選項</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="98"/>
        <source>Set hostname:</source>
        <translation>設定主機名稱：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="120"/>
        <source>Set username and password</source>
        <translation>設定使用者名稱和密碼</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="142"/>
        <source>Username:</source>
        <translation>使用者名稱：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="158"/>
        <location filename="../OptionsPopup.qml" line="219"/>
        <source>Password:</source>
        <translation>密碼：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="186"/>
        <source>Configure wireless LAN</source>
        <translation>設定無線區域網路</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="205"/>
        <source>SSID:</source>
        <translation>SSID：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="238"/>
        <source>Show password</source>
        <translation>顯示密碼</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="244"/>
        <source>Hidden SSID</source>
        <translation>隱藏 SSID</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="250"/>
        <source>Wireless LAN country:</source>
        <translation>無線網路國家/地區選項：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="261"/>
        <source>Set locale settings</source>
        <translation>設定地區設定</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="271"/>
        <source>Time zone:</source>
        <translation>時區：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="281"/>
        <source>Keyboard layout:</source>
        <translation>鍵盤配置：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="298"/>
        <source>Enable SSH</source>
        <translation>啟用 SSH</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="317"/>
        <source>Use password authentication</source>
        <translation>使用密碼驗證</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="327"/>
        <source>Allow public-key authentication only</source>
        <translation>僅允許公鑰驗證</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="345"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>設定 &apos;%1&apos; 的 authorized_keys：</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="357"/>
        <source>RUN SSH-KEYGEN</source>
        <translation>執行 SSH-KEYGEN</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="375"/>
        <source>Play sound when finished</source>
        <translation>完成時播放聲音</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="379"/>
        <source>Eject media when finished</source>
        <translation>完成時彈出媒體</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="383"/>
        <source>Enable telemetry</source>
        <translation>啟用遙測</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="397"/>
        <source>SAVE</source>
        <translation>儲存</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="119"/>
        <source>Internal SD card reader</source>
        <translation>內部 SD 卡讀卡機</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="73"/>
        <source>Use OS customization?</source>
        <translation>作業系統客製化？</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="87"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>您是否想要套用作業系統客製化設定？</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="97"/>
        <source>NO</source>
        <translation>否</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="107"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>否，清除設定</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="117"/>
        <source>YES</source>
        <translation>是</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="127"/>
        <source>EDIT SETTINGS</source>
        <translation>編輯設定</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="22"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="114"/>
        <location filename="../main.qml" line="467"/>
        <source>Raspberry Pi Device</source>
        <translation>Raspberry Pi 裝置</translation>
    </message>
    <message>
        <location filename="../main.qml" line="109"/>
        <source>CHOOSE DEVICE</source>
        <translation>選擇裝置</translation>
    </message>
    <message>
        <location filename="../main.qml" line="120"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>選擇此按鈕以選擇您的目標 Raspberry Pi</translation>
    </message>
    <message>
        <location filename="../main.qml" line="132"/>
        <location filename="../main.qml" line="413"/>
        <source>Operating System</source>
        <translation>作業系統</translation>
    </message>
    <message>
        <location filename="../main.qml" line="144"/>
        <source>CHOOSE OS</source>
        <translation>選擇作業系統</translation>
    </message>
    <message>
        <location filename="../main.qml" line="156"/>
        <source>Select this button to change the operating system</source>
        <translation>選擇此按鈕以變更作業系統</translation>
    </message>
    <message>
        <location filename="../main.qml" line="133"/>
        <location filename="../main.qml" line="780"/>
        <source>Storage</source>
        <translation>儲存裝置</translation>
    </message>
    <message>
        <location filename="../main.qml" line="145"/>
        <location filename="../main.qml" line="1108"/>
        <source>CHOOSE STORAGE</source>
        <translation>選擇儲存裝置</translation>
    </message>
    <message>
        <location filename="../main.qml" line="171"/>
        <source>WRITE</source>
        <translation>寫入</translation>
    </message>
    <message>
        <location filename="../main.qml" line="155"/>
        <source>Select this button to change the destination storage device</source>
        <translation>選擇此按鈕以變更目標儲存裝置</translation>
    </message>
    <message>
        <location filename="../main.qml" line="216"/>
        <source>CANCEL WRITE</source>
        <translation>取消寫入</translation>
    </message>
    <message>
        <location filename="../main.qml" line="219"/>
        <location filename="../main.qml" line="1035"/>
        <source>Cancelling...</source>
        <translation>正在取消…</translation>
    </message>
    <message>
        <location filename="../main.qml" line="227"/>
        <source>CANCEL VERIFY</source>
        <translation>取消驗證</translation>
    </message>
    <message>
        <location filename="../main.qml" line="230"/>
        <location filename="../main.qml" line="1058"/>
        <location filename="../main.qml" line="1127"/>
        <source>Finalizing...</source>
        <translation>正在完成…</translation>
    </message>
    <message>
        <location filename="../main.qml" line="288"/>
        <source>Next</source>
        <translation>下一步</translation>
    </message>
    <message>
        <location filename="../main.qml" line="175"/>
        <source>Select this button to start writing the image</source>
        <translation>選擇此按鈕以開始寫入映像檔</translation>
    </message>
    <message>
        <location filename="../main.qml" line="245"/>
        <source>Select this button to access advanced settings</source>
        <translation>選擇此按鈕以存取進階設定</translation>
    </message>
    <message>
        <location filename="../main.qml" line="259"/>
        <source>Using custom repository: %1</source>
        <translation>使用自訂套件庫：%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="268"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>鍵盤操作：&lt;tab&gt; 移至下一個按鈕 &lt;space&gt; 按下按鈕/選取項目 &lt;arrow up/down&gt; 在清單中上移/下移</translation>
    </message>
    <message>
        <location filename="../main.qml" line="289"/>
        <source>Language: </source>
        <translation>語言：</translation>
    </message>
    <message>
        <location filename="../main.qml" line="312"/>
        <source>Keyboard: </source>
        <translation>鍵盤：</translation>
    </message>
    <message>
        <location filename="../main.qml" line="437"/>
        <source>Pi model:</source>
        <translation>Pi 型號：</translation>
    </message>
    <message>
        <location filename="../main.qml" line="448"/>
        <source>[ All ]</source>
        <translation>[ 全部 ]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="528"/>
        <source>Back</source>
        <translation>返回</translation>
    </message>
    <message>
        <location filename="../main.qml" line="529"/>
        <source>Go back to main menu</source>
        <translation>返回主選單</translation>
    </message>
    <message>
        <location filename="../main.qml" line="695"/>
        <source>Released: %1</source>
        <translation>發布日期：%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="705"/>
        <source>Cached on your computer</source>
        <translation>已在您的電腦上快取</translation>
    </message>
    <message>
        <location filename="../main.qml" line="707"/>
        <source>Local file</source>
        <translation>本機檔案</translation>
    </message>
    <message>
        <location filename="../main.qml" line="708"/>
        <source>Online - %1 GB download</source>
        <translation>線上 - %1 GB 下載</translation>
    </message>
    <message>
        <location filename="../main.qml" line="833"/>
        <location filename="../main.qml" line="885"/>
        <location filename="../main.qml" line="891"/>
        <source>Mounted as %1</source>
        <translation>已掛載為 %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="887"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[寫入保護]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="929"/>
        <source>Are you sure you want to quit?</source>
        <translation>您確定要結束嗎？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="930"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager 仍在忙碌中。&lt;br&gt;您確定要結束嗎？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="941"/>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <location filename="../main.qml" line="949"/>
        <source>Preparing to write...</source>
        <translation>正在準備寫入…</translation>
    </message>
    <message>
        <location filename="../main.qml" line="962"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>&apos;%1&apos; 上的所有現有資料將被清除。&lt;br&gt;您確定要繼續嗎？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="973"/>
        <source>Update available</source>
        <translation>有可用的更新</translation>
    </message>
    <message>
        <location filename="../main.qml" line="974"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>有新版本的 Imager 可供下載。&lt;br&gt;您是否想前往網站進行下載？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1017"/>
        <source>Error downloading OS list from Internet</source>
        <translation>從網路下載作業系統清單時發生錯誤</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1038"/>
        <source>Writing... %1%</source>
        <translation>正在寫入… %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1061"/>
        <source>Verifying... %1%</source>
        <translation>正在驗證… %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1068"/>
        <source>Preparing to write... (%1)</source>
        <translation>正在準備寫入… (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1084"/>
        <source>Error</source>
        <translation>錯誤</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1091"/>
        <source>Write Successful</source>
        <translation>寫入成功</translation>
    </message>
    <message>
        <location filename="../main.qml" line="572"/>
        <location filename="../main.qml" line="1092"/>
        <source>Erase</source>
        <translation>清除</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1093"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 已被清除&lt;br&gt;&lt;br&gt;您現在可以從讀卡機中移除 SD 卡</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1100"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 已寫入至 &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;您現在可以從讀卡機中移除 SD 卡</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1202"/>
        <source>Error parsing os_list.json</source>
        <translation>解析 os_list.json 時發生錯誤</translation>
    </message>
    <message>
        <location filename="../main.qml" line="573"/>
        <source>Format card as FAT32</source>
        <translation>將記憶卡進行 FAT32 格式化</translation>
    </message>
    <message>
        <location filename="../main.qml" line="582"/>
        <source>Use custom</source>
        <translation>使用自訂映像檔</translation>
    </message>
    <message>
        <location filename="../main.qml" line="583"/>
        <source>Select a custom .img from your computer</source>
        <translation>從您的電腦選擇一個自訂的 .img 檔案</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1391"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>請先插入包含映像檔的 USB 隨身碟。&lt;br&gt;映像檔必須位於 USB 隨身碟的根目錄中。</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1407"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>SD 卡已啟用寫入保護。&lt;br&gt;請將卡片左側的鎖定開關向上推以解除保護，然後再試一次。</translation>
    </message>
</context>
</TS>
