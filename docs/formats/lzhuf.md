# LZHUF 压缩算法

> MKF 中 `compressed_size != decompressed_size` 的资源使用此算法。
> 逆向自 `rich4.exe`：`sub_455040`(主循环) / `sub_4551BB`(解码) / `sub_455109`(更新入口)
> / `sub_45511B`(树更新) / `sub_4550CC`(频率减半重建)。
> 属于 **自适应哈夫曼 (LZHUF) + LZSS** 变体。

## 常量

| 名称 | 值 | 说明 |
|------|-----|------|
| `SYM_COUNT` | 321 | 符号数 = 256 字面量 + 65 匹配 |
| `T` (根节点) | 640 | 哈夫曼树根，son 字节偏移 `0x500` |
| `MAX_FREQ` | 0x8000 | 根频率达此值时触发重建 |
| `LEAF_THRESHOLD` | 0x502 | son 值 ≥ 此值为叶子 |
| 位序 | LSB-first | 从低字节低位开始 |

## 运行时工作区

`sub_455040` 开始时将 exe 中 `unk_483630` 的 **4492 字节**静态表复制到 `word_4847BC`，
作为自适应树的初始状态。数组以**字节偏移**寻址（节点索引 × 2）：

| 基址 (VA) | 数组 | 元素 | 说明 |
|-----------|------|------|------|
| `0x4847BC` | `freq[642]` | i16 | 频率，`freq[640]` 为根，`freq[641]=0xFFFF` 哨兵 |
| `0x484CC0` | `son[641]` | i16 | 子节点字节偏移；叶子值 = `0x502 + 2*symbol` |
| `0x4851C2` | `prnt[641]` | i16 | 父节点字节偏移 |
| `0x4856C4` | `sym2node[321]` | i16 | 符号 → 节点偏移（本作中为恒等映射） |

另有 `byte_483430`(256B, offset 高 6 位表) 与 `byte_483530`(256B, 码长表) 用于匹配偏移解码。

## 解码流程

```
初始化工作区 (复制 4492B)
loop:
    symbol = DecodeChar()            # 自适应哈夫曼
    if symbol < 256:
        输出字节 symbol
    else:
        bits = 从位流读 32 位
        b    = bits & 0xFF
        L    = byte_483530[b]        # 前缀码长
        hi   = byte_483430[b]        # 高 6 位
        offset = (hi << 6) | ((bits >> L) & 0x3F)   # 12 位
        bitpos += L + 6
        if offset == 0xFFF: break    # 结束标记
        length = symbol - 253        # 3..67
        从输出缓冲回退 (offset+1) 字节, 复制 length 字节 (LZ77)
```

### DecodeChar (`sub_4551BB`)

```
v2 = 0x500                          # 根节点字节偏移
while son[v2] < 0x502:              # 非叶子
    bit = next_bit()
    v2  = son[v2] + (bit ? 2 : 0)
ebx = son[v2] - 0x502
UpdateTree(ebx)                     # sub_455109 → sub_45511B
return ebx >> 1                     # 符号
```

### UpdateTree / 重建

- `sub_455109`：若 `freq[640] == 0x8000` 先调用 `sub_4550CC` 重建（对奇数频率节点重新更新后整体右移一位），再 `sub_45511B`
- `sub_45511B`：沿 `prnt` 链向上 `freq++`，并按频率排序规则交换节点（含 son/prnt 双向修正）
- `sub_4550CC`：遍历 321 个符号，频率为奇数的节点先更新，随后 `freq[i] >>= 1`

> 注意：`sub_455040` 中查找同频节点的循环是 `do { ... } while` 语义 —— **先 `v2 += 2` 再判断相等**。
> 翻译为高级语言时若写成先判断后自增，会导致交换节点偏移差 1（本项目调试中曾遇到此坑）。

## 参考实现

见 [`tools/lzhuf.py`](../../tools/lzhuf.py)。`LZHUF.decompress(src, out_size)` 可直接调用；
每次解压会重置工作区（自适应树有状态，不可复用）。

## 验证

- `Data.mkf` 全部 310 个压缩资源解压长度与 `decompressed_size` 完全一致
- 解压结果魔数分布合理（SPR/SMP/像素数据）
- 首个资源解压得到 `SMP\0` 头，图像可正常渲染
