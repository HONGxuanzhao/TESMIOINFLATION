# INFLATION / POP 本地兼容更新

更新日期：2026-10-01。适用游戏：WRSR 1.1.1.9（PE 时间戳 6A3EB6AD）。

| 项目 | 新版 |
|---|---|
| TESMIOINFLATION | 1.5.0-reputation2.1 |
| TESMIOPOPMOD | 0.4.0-reputation2.1 |
| Reputation 适配目标 | 当前 2.1 DLL，时间戳 6AB18B03，SizeOfImage 6B2000 |

本次只更新两个 MOD 的源码、DLL、说明和其自身 INI。Reputation.dll、SOVIET64.exe 和资源声明列表没有在磁盘上修改。未发布 Steam 更新。

## 实现行为

### 正常价格与事件价格

INFLATION 保留原生经济计算和原有私有常量补丁。新增兼容层包围原生经济更新（RVA 2FB1D0）及生产成本重算（2A9340）：先恢复正常价格，执行原生计算，再捕获新的正常价格，并交给 Reputation 原有价格程序叠加当前事件/阵营倍率。

`最终价格(t) = 正常价格(t) × 当前 Reputation 倍率(t)`。

Reputation 每日定价（25F0）前，其已缓存资源的价格基准随正常价格更新。事件结束恢复当时的正常价格；不会恢复到首次缓存的旧价，也不会每天对已受事件影响的价格再次乘倍率。嵌套的成本重算只在最外层施加事件一次。切换世界时恢复最新正常价格并清空兼容快照。

当前 Reputation 把 RUB/USD 字段颠倒。兼容层在运行内存中修正其六处资源价格读写指令，使 RUB 对应 +5C，USD 对应 +58；包含首次缓存、每日写价和世界关闭恢复。DLL 文件保持原样。垃圾负价格仍保留，不设置统一正数下限。

保持 Reputation `[events] price_effects=1` 才有价格效果。本次保留它当前的设置。

### 贷款

贷款公式仍是 INFLATION 原有的可配置公式：

```text
A_same/other = 对应币种的 CurrentAmount + CurrentAmountForPenalty 总和
P_same/other = 对应币种的 CurrentAmountForPenalty 总和
q = 当前该币种钢价 / loan_reference_steel_price
D = (A_same + w*A_other)/q/loan_balance_debt_scale
  + (P_same + w*P_other)/q/loan_penalty_debt_scale
r = (币种基础利率 + D) * (1 + loan_term_factor*期限天数/loan_days_per_year)
```

事件改变钢价时通过 q 影响债务压力；无债务时钢价不影响计算出的利率。未额外添加 Reputation 中不存在的独立利率倍率。只作用于新贷款/预览，已有贷款不重新定价。

修复：贷款计算直接读取 gameObject+C2B0/C2B8 的实时资源向量，按 832 字节记录查找 steel，并校验边界、名称和价格。不缓存资源指针。移除已经不需要的 ResourceGet 原始入口校验，保留贷款函数和预览指令校验、私有常量及回滚。因此 resources.dll 先加载也不会再把贷款功能关闭。

### 人口

POP 保留原生出生条件、累积阈值、玩家已有研究、健康等输入，以及原有死亡候选系数。正常基础系数仍取自己的 INI。

```text
出生累积增量 = 原生条件计算 C × POP 出生基础系数 × 原生研究因子 × Reputation 出生倍率
```

兼容层调用经过完整函数指纹核验的 Reputation 2.1 出生倍率计算函数（77A0），保留它对真实激活事件和符合条件阵营的组合规则；不重新解析事件表、不猜测事件触发时间。因此 reputation.d 扩展事件也由 Reputation 自己决定是否生效。

POP 在原有地形渲染调用链执行后更新私有出生系数；使用原始配置值乘倍率一次。为避免重复施加，运行内存中关闭 Reputation 的出生研究开关写入和居民出生累积量直接写入。其移民、逃亡、状态、外交、合同等代码保留。Reputation 的研究丢失等独立事件效果也保留。

此倍率作用于出生累积增量，并非全国出生人数的固定同比比例。阵营出生效果也会保留，例如满足条件的不结盟阵营倍率。

### 没有 Reputation

两个 MOD 在 Start 阶段查询可选服务。不存在 Reputation 服务时不安装适配挂钩、不改变其他 MOD；基础价格/贷款补丁与 POP 四个基础系数照常生效。两个 MOD 也分别测试了单独配合 Reputation 的启动路径。

两个 INI 都新增：

```ini
reputation_compat_enabled=1
```

设为 0 关闭该 MOD 的 Reputation 兼容层，保留自己的基础补丁。兼容层只支持当前已核验的 2.1 二进制；其他版本被拒绝适配并写日志，不能解释为与任意未来版本兼容。

## 零价格诊断

本地扫描 1799 个 INI，覆盖 73 个有 workshopconfig.ini 的下载项目、原版/DLC 建筑及 VFS。完整逐文件结果见 `zero-price-audit.json`，脚本为 `audit_zero_prices.py`。

| 资源 | 当前本地生产定义 | 判断 |
|---|---|---|
| copper_ore、copper_concentrate、raw_copper、copper | 0 | 只声明了商品，缺少铜产业链生产定义 |
| gas、sand、clay、glass | 0 | 只声明了商品，缺少对应生产定义 |
| medicine、books | 0 | 缺少药品、书籍生产定义；商店需求和图标不能替代生产配方 |
| furniture | 1，3773757889/FurnitureFactory/building.ini:41 | 已有家具工厂，不能再归为缺少生产 MOD |
| small_arms、large_arms、munitions | 各 2，3797633240 | 六座武器工厂已经存在，截图中这些商品也有非零价格 |

对于这批克隆的、需生产成本定价的额外资源，原生 2A9340 价格程序调用 2A94E0 成本求解器。求解器找不到生产该资源的建筑类型时会返回 0；之后 0 被写入资源价格。复制 eletronics、steel 等模板只让资源具有运输/存储等基础属性，不等于复制了一个生产它的工厂。INFLATION 和事件倍率都不能把 0 自动变成非零。

**是否缺少 MOD：**上述十种没有生产定义的资源，当前缺的是对应产业建筑/配方。若原本计划由某个工业扩展提供它们，需要安装或恢复那个扩展；现有资料不能确认十种商品分别要求哪个具体创意工坊项目。不能据此断言缺少某个指定依赖。Reputation 附带的建筑项目主要提供外交机构和武器工厂，不提供这些完整产业链。

**家具的新证据：**工厂文件在今天 20:46 更新；最新资源日志已出现 furniture、caller_rva=110F2F、返回非空。原生该调用点位于 `$PRODUCTION` 解析分支，说明游戏已经成功解析该生产资源，而不只是文件存在。配方为每工人 furniture 0.012，消耗 boards 0.020、fabric 0.008，80 工人。截图中的家具零价不能直接代表这次更新后的状态。重启并让成本重算执行后需观察新价格；如果仍为 0，检查新日志、实际价格和输入资源成本，不能继续简单归因于未下载工厂。

另外，现有 resources.dll 是 1.1，配置中的新 `price_hook`、`[base_price]`、`[price]` 等字段不能仅因写在 INI 中就认为被它支持。固定价格补丁还会绕过生产成本或覆盖通胀，因此本次没有用人为固定价格掩盖缺配方问题。books 缺图标会影响显示，但图标缺失不是价格计算为 0 的原因。

可行修复是补齐使用相同资源标识的生产建筑/配方，再重启让建筑类型载入并重算。若使用固定价格方案，需要兼容的资源插件及明确的基础价格设计。本次保留资源列表、顺序及计数；未删除零价资源，避免破坏存档格式。

## 验证与边界

MSVC x64 编译通过。离线执行测试共 7 种场景、63 个断言，失败为 0：standalone、withrep、unsupported、rollback、disabled、poponly、infonly。日志在 `build/test-*.log`。

测试加载实际新版 DLL，并执行原始 Reputation 2.1 的定价/恢复/出生倍率函数；资源、债务、事件及原生经济更新输入由测试宿主构造。验证了双币种不同倍率、跨通胀更新、事件结束、嵌套成本重算、资源向量搬迁、负垃圾费、零价格、出生倍率、无服务回退及安装失败回滚。

2026-10-01 22:45 实际启动日志确认：两项新版均加载成功，贷款公式挂钩和 14 处预览同步均成功，价格/人口 Reputation 2.1 适配器启用。完整日志见 `runtime-startup.log`。只读进程检查确认私有出生系数约 0.4，两条 Reputation 出生写入开关均为 0，当前 in_world=0，资源向量尚未初始化；结果见 `runtime-prices.json`。这不能据此判断新商品价格已经正常。

这不是长时间游戏或存档实测。仍需在自己的存档副本中观察事件开始/结束及跨五日经济更新的价格和新贷款预览；已有世界若曾被事件直接修改人口数据，安装本更新不会逆转历史变化。此次启动未自动读取或保存存档，游戏保留打开供继续观察。

## 备份与回退

旧版源文件、DLL 和安装 INI 位于 `backup/TESMIOINFLATION`、`backup/TESMIOPOPMOD`。若同步了 workshop_wip，原包另存于 `backup/workshop_wip`。关闭游戏后运行本目录 `rollback.ps1` 可恢复安装 DLL/INI；它不会删除源码更新、资源声明、其他 MOD 或存档。

新版 DLL 同步至游戏 build/plugins、桌面两个 MOD 包，以及匹配原源码的本地 workshop_wip 包。Steam 下载缓存和在线创意工坊没有发布或覆盖。
