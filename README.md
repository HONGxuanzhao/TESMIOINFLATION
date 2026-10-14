# TESMIOINFLATION

[GitHub](https://github.com/HONGxuanzhao/TESMIOINFLATION) | [Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3792616272)

## Download and installation / 下载与安装

The ready-to-use plugin files are in `plugins/`. Download this repository using **Code → Download ZIP**, extract it, and follow the instructions below.

可直接使用的插件文件位于 `plugins/`。点击 **Code → Download ZIP** 下载并解压，按下文安装。

## Package instructions / 使用说明

TESMIOINFLATION — TESMIO / Republic Mod Loader 安装说明
====================================================

运行包结构
README.txt、previewimage.png 放在根目录；插件 DLL、INI 和 RML 元数据放在 plugins 目录。
本包面向 WRSR 1.1.1.9（64 位）。DLL 使用 TESMIO API 4；RML 使用其 TESMIO 兼容宿主加载。

TESMIO 安装
1. 完全退出游戏，从本包 plugins 目录复制 TESMIOINFLATION.dll 和 TESMIOINFLATION.ini 到：
   <游戏目录>\tesmioloader\build\plugins\（tesmioloader.dll 旁的 plugins 目录）。
2. 启用 TESMIO 插件扫描；若 [plugins] 显式关闭了 TESMIOINFLATION，将其设为 1。
3. 使用 tesmiolauncher.exe 启动。RML 的 .rml.json 元数据无需复制给 TESMIO。

Republic Mod Loader（RML）安装
1. 订阅并下载对应创意工坊项目后，使用支持 TESMIO API 4 的 RML 启动器。
2. 刷新列表，在 Plugins 中启用 TESMIOINFLATION；工坊文件应位于 <项目编号>\plugins\。
3. 手动安装时，将本包 plugins 内的 DLL、INI、.rml.json 复制到 RML 的 rml\plugins\。
4. INI 与实际加载的 DLL 放在同一目录。若已有手动安装副本，先备份个人设置再统一安装来源。

配置与发布
编辑实际加载目录内的 INI；保存为 UTF-8 无 BOM，并完整重启游戏。
同一个游戏进程选择一个 Loader 启动。可选的 Reputation 联动不构成强制依赖。
上传目录应包含 plugins、README.txt、previewimage.png 及该项目原有的 workshopconfig.ini。
保留各项目的 ITEM_ID、OWNER_ID 和可见性，使用 SCRIPT 项目类型；不要上传传输 ZIP。
本次调整验证了打包结构和接口；调整参数后的游戏效果及存档兼容性仍需实际运行验证。

English installation
====================
Target: WRSR 1.1.1.9 x64; TESMIO API 4 or RML with the TESMIO API 4 compatibility host.
TESMIO: copy TESMIOINFLATION.dll and TESMIOINFLATION.ini from this package's plugins folder into
<game>\tesmioloader\build\plugins, enable the plugin, and launch with tesmiolauncher.exe.
RML Workshop: subscribe/download the item, refresh the inventory and enable TESMIOINFLATION.
RML manual: copy DLL, INI and .rml.json from plugins into RML's rml\plugins folder.
Keep the INI beside the DLL that is actually loaded. Edit settings, then fully restart.
Use one loader per game process. Reputation integration is optional, not a required dependency.
Publish the plugins folder with root README/preview and the item's original workshopconfig.ini;
preserve item/owner IDs and visibility, use SCRIPT type, and do not upload the transfer ZIP.
Package/interface checks do not establish gameplay behavior or save compatibility.

功能与参数 / Features and settings
====================================================
TESMIOINFLATION 1.5.0-reputation2.1
中文 / English

中文
====
功能
- 调整卢布、美元的通胀范围，以及市场和资源价格的随机波动。
- 调整新贷款利率的计算参数，并同步“经济与贸易”面板的利率预览。
- 可与已核验的 Reputation 2.1 联动，让事件价格效果与正常价格变化共同生效。
价格仍受游戏供需等因素影响，配置中的通胀范围不等于最终价格涨幅。
贷款设置只影响新贷款；已有贷款的合同利率保持原样。

要求与安装
需要 WRSR 1.1.1.9（64 位）和支持 TESMIO API 4 的 TESMIO 或 RML。可在中文或英文游戏中使用。
1. 将 TESMIOINFLATION.dll 和 TESMIOINFLATION.ini 复制到：
   <游戏目录>\tesmioloader\build\plugins\
2. 在 build\tesmioloader.ini 的 [plugins] 段加入 TESMIOINFLATION=1。
3. 通过 tesmiolauncher.exe 启动游戏。使用 TESMIO 时，仍需手动安装 DLL 和 INI；使用 RML 时按开头的订阅安装步骤操作。

常用设置（编辑 TESMIOINFLATION.ini，保存后完全退出并重启游戏）
- inflation_min_percent、rub_inflation_max_percent、usd_inflation_max_percent：
  两种货币的通胀范围。
- market_noise_min/max：市场价格随机波动范围。
- resource_market_noise_min/max：资源额外随机波动范围；不是百分数。
- loan_rates_enabled：贷款利率调整开关。
- loan_preview_sync_enabled：贷款利率预览同步开关。
- reputation_compat_enabled：Reputation 2.1 联动开关；没有 Reputation 也能使用基础功能。
每组 _min/max 表示对应的 _min 和 _max 两个键。其他贷款参数见 INI 注释。
详细技术说明见 COMPATIBILITY_UPDATE.md。

English
=======
Features
- Adjust RUB/USD inflation ranges and random market/resource price fluctuations.
- Configure interest calculations for new loans and synchronize the rate preview
  in the Economy & Trade panel.
- Optional integration with the verified Reputation 2.1 build combines its event
  price effects with normal price changes.
Supply, demand and other game factors still affect prices; configured inflation
ranges are not guaranteed final price increases. Existing loan rates stay unchanged.

Requirements and installation
Requires WRSR 1.1.1.9 (64-bit) and TESMIO or RML with TESMIO API 4 support. Works with Chinese or English game text.
1. Copy TESMIOINFLATION.dll and TESMIOINFLATION.ini to:
   <game folder>\tesmioloader\build\plugins\
2. Add TESMIOINFLATION=1 under [plugins] in build\tesmioloader.ini.
3. Launch through tesmiolauncher.exe. For TESMIO, manually install the DLL and INI; for RML, follow the subscription instructions above.

Main settings
Edit TESMIOINFLATION.ini, save it, then fully exit and restart the game.
- inflation_min_percent / rub_inflation_max_percent / usd_inflation_max_percent:
  inflation ranges for the two currencies.
- market_noise_min/max: random market price fluctuations.
- resource_market_noise_min/max: extra resource price fluctuations; not percentages.
- loan_rates_enabled: enable custom loan interest calculations.
- loan_preview_sync_enabled: synchronize the loan rate preview.
- reputation_compat_enabled: enable Reputation 2.1 integration; base features also
  work without Reputation.
Each _min/max pair refers to separate _min and _max keys. See the INI comments for
other loan settings. Technical details: COMPATIBILITY_UPDATE.md (Chinese).

GitHub: https://github.com/HONGxuanzhao/TESMIOINFLATION
