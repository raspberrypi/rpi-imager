<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>解压至 %1 时发生错误</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>挂载 FAT32 分区时发生错误</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>操作系统未能挂载 FAT32 分区</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>进入文件夹 “%1” 时发生错误</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>读取存储设备时发生错误。&lt;br&gt;SD 卡可能已损坏。</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>写入校验失败。SD 卡与写入的内容不一致。</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>卸载存储设备</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>打开存储设备</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>运行 “diskpart” 命令时发生错误：%1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>删除现有分区时发生错误</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>验证已取消</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>在运行 authopen 以获取对磁盘设备 “%1” 的访问权限时发生错误</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>请检查，在隐私设置中是否允许树莓派启动盘制作工具（Raspberry Pi Imager）访问“可移除的宗卷”（位于“文件和文件夹”下），或为其授予“完全磁盘访问权限”。</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>无法打开存储设备 “%1”。</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>清除存储设备上的已有数据</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>将存储设备的首尾 1 M 清零</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>清零 MBR 时写入错误</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>在对设备尾部清零时写入错误&lt;br&gt;SD 卡标称的容量错误（可能是扩容卡）。</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>开始下载</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>下载错误，已下载：%1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>将文件写入磁盘时被拒绝访问。</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>似乎已启用受控文件夹访问权限。 请将 rpi-imager.exe 和 fat32format.exe 添加至允许的应用程序列表中，然后重试。</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>将文件写入磁盘时发生错误</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>写入存储设备时发生错误</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>在写入存储设备时（fsync）发生错误</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>在写入第一个区块（分区表）时发生错误</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>读取存储设备时发生错误。&lt;br&gt;SD 卡可能已损坏。</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>写入校验失败。SD 卡与写入的内容不一致。</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>使用自定义镜像</translation>
    </message>
    <message>
        <source>缓存文件已损坏。SHA256 哈希值与预期值不匹配。&lt;br&gt;该缓存文件将被删除，将重新开始下载。</source>
        <translation></translation>
    </message>
    <message>
        <source>本地文件已损坏或其 SHA256 哈希值不正确。&lt;br&gt;预期值：%1&lt;br&gt;实际值：%2</source>
        <translation></translation>
    </message>
    <message>
        <source>下载内容似乎已损坏。SHA256 哈希值不匹配。&lt;br&gt;预期值：%1&lt;br&gt;实际值：%2&lt;br&gt;请检查您的网络连接，然后重试。</source>
        <translation></translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error partitioning: %1</source>
        <translation>分区时发生错误：%1</translation>
    </message>
    <message>
        <source>Error starting fat32format</source>
        <translation>fat32format 执行错误</translation>
    </message>
    <message>
        <source>Error running fat32format: %1</source>
        <translation>运行 fat32format 时发生错误：%1</translation>
    </message>
    <message>
        <source>Error determining new drive letter</source>
        <translation>确定新驱动器号时发生错误</translation>
    </message>
    <message>
        <source>Invalid device: %1</source>
        <translation>无效的设备：%1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>格式化时发生错误</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>暂不支持在该平台上进行格式化</translation>
    </message>
    <message>
        <source>Error opening device for formatting</source>
        <translation>格式化时设备打开失败</translation>
    </message>
    <message>
        <source>Error writing to device during formatting</source>
        <translation>格式化过程中写入设备时出错</translation>
    </message>
    <message>
        <source>Error seeking on device during formatting</source>
        <translation>格式化过程中定位设备时出错</translation>
    </message>
    <message>
        <source>Invalid parameters for formatting</source>
        <translation>格式化参数无效</translation>
    </message>
    <message>
        <source>Insufficient space on device</source>
        <translation>设备空间不足</translation>
    </message>
    <message>
        <source>Unknown formatting error</source>
        <translation>未知的格式化错误</translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation>储存设备</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation>找不到存储设备</translation>
    </message>
    <message>
        <source>Exclude System Drives</source>
        <translation>排除系统驱动器</translation>
    </message>
    <message>
        <source>gigabytes</source>
        <translation>千兆字节</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation>已挂载为：%1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation>GB</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation>[写保护]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation>系统</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>SD 卡写保护。&lt;br&gt;请尝试向上推动 SD 卡左侧的锁定开关，然后再试一次。</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation>选择设备</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>树莓派设备</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>存储容量不足。&lt;br&gt;至少需要 %1 GB 的剩余空间.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>所选文件是无效的磁盘镜像。&lt;br&gt;文件大小 %1 B 不是 512 B 的整倍数。</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>下载和写入镜像</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>选择镜像</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>时间同步错误。将在 3 秒后重试</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>您的以太网交换机启用了 STP。获取 IP 地址可能需要较长时间。</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>您想要通过系统钥匙串访问自动填充 WiFi 密码吗？</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>打开镜像文件</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>读取镜像文件时发生错误</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation>开始解压</translation>
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
        <translation>确认</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>继续</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>退出</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <source>Recommended</source>
        <translation>建议</translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <source>Operating System</source>
        <translation>操作系统</translation>
    </message>
    <message>
        <source>Back</source>
        <translation>返回</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation>回到主菜单</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation>发布时间：%1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation>已缓存到本地磁盘</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation>本地文件</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation>还需下载：%1 GB</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>请先接入一款包含镜像的 USB 设备。&lt;br&gt;其镜像必须位于该设备的根路径下。</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation>设置主机名：</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation>设置用户名及密码</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation>用户名：</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation>配置 WLAN</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation>网络名称：</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="212"/>
        <source>Show password</source>
        <translation>显示密码</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="223"/>
        <source>Hidden SSID</source>
        <translation隐藏的网络</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation>WLAN 区域：</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation>本地化设置</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation>时区：</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation>键盘布局：</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation>在完成后播放提示音</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation>在完成后卸载磁盘</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation>启用遥测</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>自定义系统配置</translation>
    </message>
    <message>
        <source>General</source>
        <translation>通用</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>服务</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>可选配置</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="123"/>
        <source>SAVE</source>
        <translation>保存</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation>开启 SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation>使用密码登录</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation>仅使用公钥登录</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>为用户 “%1” 设置 authorized_keys：</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation>删除密钥</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation>运行 SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation>添加 SSH 密钥</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="41"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>您想应用自定义系统配置吗？</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>否</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>否，并清空所有设置</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>确认</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>编辑设置</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Raspberry Pi Imager v%1</source>
        <translation>树莓派启动盘制作工具 v%1</translation>
    </message>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>树莓派设备</translation>
    </message>
    <message>
        <location filename="../main.qml" line="144"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>点击此处选择您的树莓派型号</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>操作系统</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>选择操作系统</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>点击此处更改操作系统</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>储存设备</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>网络不可用</translation>
    </message>
    <message>
        <location filename="../main.qml" line="207"/>
        <location filename="../main.qml" line="668"/>
        <source>CHOOSE STORAGE</source>
        <translation>选择储存设备</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>点击此处更改所选存储设备</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>取消写入</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>正在取消……</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>取消校验</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>正在完成...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>下一步</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>点击此处开始写入</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>使用自定义存储库（repository）：%1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>键盘导航：&lt;tab&gt; 切换选项 &lt;space&gt; 确认/选择后使用 &lt;arrow up/down&gt; 可在列表中上下移动</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>语言：</translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>键盘：</translation>
    </message>
    <message>
        <location filename="../main.qml" line="485"/>
        <source>Are you sure you want to quit?</source>
        <translation>您确定要退出吗？</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>树莓派启动盘制作工具还未完成任务。&lt;br&gt;您确定要退出吗？</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>准备写入……</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>&apos;%1&apos; 上的已有数据都将被删除。&lt;br&gt;是否继续？</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>检测到更新</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>检测到新版树莓派启动盘制作工具。&lt;br&gt; 是否转到网页进行下载？</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>正在写入…… %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>正在校验…… %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>准备写入…… (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>写入成功</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>格式化</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1 &lt;/ b&gt; 已成功格式化&lt;br&gt; &lt;br&gt;您现在可以从读卡器上取下 SD 卡了</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 已成功写入至 &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;，您现在可以从读卡器上取下 SD 卡了</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="649"/>
        <source>Format card as FAT32</source>
        <translation>把 SD 卡格式化为 FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>使用自定义镜像</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>选择本地已有的 .img 文件</translation>
    </message>
    <message>
        <source>SKIP CACHE VERIFICATION</source>
        <translation>跳过缓存验证</translation>
    </message>
    <message>
        <source>Starting download...</source>
        <translation>开始下载……</translation>
    </message>
    <message>
        <source>Verifying cached file... %1%</source>
        <translation>正在验证缓存文件……</translation>
    </message>
    <message>
        <source>Verifying cached file...</source>
        <translation>正在验证缓存文件……</translation>
    </message>
    <message>
        <source>Starting write...</source>
        <translation>开始写入……</translation>
    </message>
</context>
</TS>
