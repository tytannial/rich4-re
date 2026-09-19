/*
 * 大富翁4 (Rich4) 资源与数据格式定义
 * 由 rich4.exe 逆向重建, 详见 docs/formats/*.md
 */
#ifndef RICH4_FORMATS_H
#define RICH4_FORMATS_H

#include <stdint.h>

#pragma pack(push, 1)

/* ---- MKF 资源容器 (docs/formats/mkf.md) ---- */
typedef struct {
    uint32_t decompressed_size;
    uint32_t compressed_size;
    uint32_t data_offset;      /* 需做像素格式转换的起始 (v10) */
    uint32_t data_length;      /* 需转换的长度 (v11) */
} MkfEntryHeader;

/* 索引表: u32 index[N], index[0]=4, index[i]=第 i-1 个资源头偏移, N=(size-index_offset)/4 */

/* ---- SPR 8bit 调色板精灵 (docs/formats/spr.md) ---- */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t x;
    uint16_t y;
    uint32_t size;             /* width * height */
} SprFrame;

typedef struct {
    uint32_t magic;            /* 'SPR\0' = 0x00525053 */
    uint32_t count;
    uint32_t data_offset;      /* = 12 + count*12 */
    /* SprFrame frames[count]; */
    /* uint16_t  palette[256];   512B, RGB555 */
    /* uint8_t   pixels[];       8bit 索引, 逐帧连续, 索引 0 透明 */
} SprHeader;

/* ---- SMP 16bit 高彩位图 (docs/formats/smp.md) ---- */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t x;
    uint16_t y;
    uint32_t size;             /* width * height * 2 */
} SmpFrame;

typedef struct {
    uint32_t magic;            /* 'SMP\0' = 0x00504D53 */
    uint32_t count;
    uint32_t data_offset;      /* = 12 + count*12 */
    /* SmpFrame frames[count]; */
    /* uint16_t pixels[];        16bit 直接色 (RGB555), 逐帧连续, 0 透明 */
} SmpHeader;

/* ---- GND 地图地面 (docs/formats/map.md) ---- */
typedef struct {
    uint32_t magic;            /* 'GND\0' = 0x00444E47 */
    uint16_t grid_w;           /* 72 */
    uint16_t grid_h;           /* 72 */
    uint32_t count;            /* 5184 = grid_w * grid_h */
    uint32_t reserved;         /* 0 */
    /* uint16_t block[256];       512B, 经像素格式转换 */
    /* uint8_t  cells[count * 1026]; */
} GndHeader;

/* ---- RIFF/WAVE 音频 ---- */
typedef struct {
    uint32_t riff;             /* 'RIFF' */
    uint32_t size;
    uint32_t wave;             /* 'WAVE' */
    /* ... fmt / data chunks ... 22050Hz, 8bit, mono PCM */
} RiffHeader;

/* ---- RICH4.CFG 配置 (docs/formats/cfg.md) ---- */
typedef struct {
    uint8_t  settings[16];
    uint16_t keys[28];         /* 虚拟键码, 见 cfg.md 索引语义 */
} Rich4Cfg;

/* ---- SAVE%d.DAT 存档 (docs/formats/save.md) ---- */
typedef struct {
    uint32_t version;          /* 38 */
    uint32_t datetime;
    uint16_t field8;
    uint16_t field10;
    uint32_t player_count;
    /* Player players[4];        每玩家 104 字节 */
    /* ... 游戏状态 ... */
} SaveHeader;

#pragma pack(pop)

#endif /* RICH4_FORMATS_H */
