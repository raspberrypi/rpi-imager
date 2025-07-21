<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_TW">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>解壓縮檔案時發生錯誤：%1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>掛載 FAT32 分割區時發生錯誤</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>作業系統未掛載 FAT32 分割區</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>變更至目錄 &apos;%1&apos; 時發生錯誤</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">從儲存裝置讀取時發生錯誤。&lt;br&gt;SD 卡可能已損壞。</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">驗證寫入失敗。SD 卡的內容與寫入的內容不一致。</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>正在卸載磁碟</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>正在開啟磁碟</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>執行 diskpart 時發生錯誤：%1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>移除現有分割區時發生錯誤</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>已取消驗證</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>執行 authopen 以取得磁碟裝置 &apos;%1&apos; 存取權限時發生錯誤</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>請確認在隱私設定中是否允許 &apos;Raspberry Pi Imager&apos; 存取 &apos;可移除磁碟&apos;（在 &apos;檔案和資料夾&apos; 下，或者給予它 &apos;完全磁碟存取權&apos;）。</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>無法開啟儲存裝置 &apos;%1&apos;。</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>正在丟棄磁碟上的現有資料</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>正在將磁碟的第一個和最後一個 MB 清除</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>在將 MBR 清除時寫入錯誤</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>嘗試將卡的最後部分清除時寫入錯誤。&lt;br&gt;卡可能在廣告錯誤的容量（可能是偽造的）。</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>開始下載</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>下載時發生錯誤：%1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>寫入檔案至磁碟時存取被拒絕的錯誤。</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>寫入檔案至磁碟時發生錯誤</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>寫入儲存裝置時發生錯誤（在清除時）</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>寫入儲存裝置時發生錯誤（在 fsync 時）</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>寫入第一個區塊（分割表）時發生錯誤</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>從儲存裝置讀取時發生錯誤。&lt;br&gt;SD 卡可能已損壞。</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>驗證寫入失敗。SD 卡的內容與寫入的內容不一致。</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>自訂映像檔</translation>
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
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add rpi-imager.exe to the list of allowed apps and try again.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>格式化時發生錯誤（透過 udisks2）</translation>
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
    <message>
        <source>Cannot format device: insufficient permissions and udisks2 not available</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation type="unfinished">儲存裝置</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="unfinished">找不到儲存裝置</translation>
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
        <translation type="unfinished">已掛載為 %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[寫入保護]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD 卡已啟用寫入保護。&lt;br&gt;請將卡片左側的鎖定開關向上推以解除保護，然後再試一次。</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">選擇裝置</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Raspberry Pi 裝置</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>儲存容量不足。&lt;br&gt;至少需要 %1 GB。</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>輸入檔案不是有效的磁碟映像檔。&lt;br&gt;檔案大小 %1 位元組不是 512 位元組的倍數。</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>正在下載並寫入映像檔</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>選擇映像檔</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>時間同步失敗。將在 3 秒後重試</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>您的網路交換器已啟用 STP 功能，取得 IP 位址需要較長時間</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>您是否想要從系統鑰匙圈預填 Wi-Fi 密碼？</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>正在開啟映像檔</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>開啟映像檔時發生錯誤</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error reading from image file</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error writing to device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Failed to read complete image file</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <source>NO</source>
        <translation>否</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>是</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>繼續</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>結束</translation>
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
        <translation type="unfinished">作業系統</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">返回</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">返回主選單</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">發布日期：%1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">已在您的電腦上快取</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">本機檔案</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">線上 - %1 GB 下載</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">請先插入包含映像檔的 USB 隨身碟。&lt;br&gt;映像檔必須位於 USB 隨身碟的根目錄中。</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">設定主機名稱：</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">設定使用者名稱和密碼</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">使用者名稱：</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">密碼：</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">設定無線區域網路</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID：</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">隱藏 SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">無線網路國家/地區選項：</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">設定地區設定</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">時區：</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">鍵盤配置：</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">完成時播放聲音</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">完成時彈出媒體</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">啟用遙測</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>作業系統客製化</translation>
    </message>
    <message>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>服務</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>選項</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>儲存</translation>
    </message>
    <message>
        <source>CANCEL</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in General and Services tabs</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in General tab</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in Services tab</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">啟用 SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">使用密碼驗證</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">僅允許公鑰驗證</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">設定 &apos;%1&apos; 的 authorized_keys：</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">執行 SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Invalid SSH key format. SSH keys must start with ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, or SSH certificates, followed by the key data and optional comment.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Paste your SSH public key here.
Supported formats: ssh-rsa, ssh-ed25519, ssh-dss, ecdsa-sha2-nistp, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, and SSH certificates
Example: ssh-rsa AAAAB3NzaC1yc2E... user@hostname</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation>您是否想要套用作業系統客製化設定？</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>否</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>否，清除設定</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>是</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>編輯設定</translation>
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
        <translation>Raspberry Pi 裝置</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>選擇此按鈕以選擇您的目標 Raspberry Pi</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>作業系統</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>選擇作業系統</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>選擇此按鈕以變更作業系統</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>儲存裝置</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>網路尚未就緒</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>選擇儲存裝置</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>選擇此按鈕以變更目標儲存裝置</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>取消寫入</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>正在取消…</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>取消驗證</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>正在完成…</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>下一步</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>選擇此按鈕以開始寫入映像檔</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>使用自訂套件庫：%1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>鍵盤操作：&lt;tab&gt; 移至下一個按鈕 &lt;space&gt; 按下按鈕/選取項目 &lt;arrow up/down&gt; 在清單中上移/下移</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>語言：</translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>鍵盤：</translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>您確定要結束嗎？</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager 仍在忙碌中。&lt;br&gt;您確定要結束嗎？</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>正在準備寫入…</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>&apos;%1&apos; 上的所有現有資料將被清除。&lt;br&gt;您確定要繼續嗎？</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>有可用的更新</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>有新版本的 Imager 可供下載。&lt;br&gt;您是否想前往網站進行下載？</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>正在寫入… %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>正在驗證… %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>正在準備寫入… (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>錯誤</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>寫入成功</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>清除</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 已被清除&lt;br&gt;&lt;br&gt;您現在可以從讀卡機中移除 SD 卡</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 已寫入至 &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;您現在可以從讀卡機中移除 SD 卡</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>將記憶卡進行 FAT32 格式化</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>使用自訂映像檔</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>從您的電腦選擇一個自訂的 .img 檔案</translation>
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
