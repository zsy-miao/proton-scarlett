#include <stdint.h>
#include "bvh_record.h"
#include "shader_digest.h"
#include "bvh_init_dxil.h"
#include "bvh_offline_dxil.h"
#include "bvh_partitions_dxil.h"
#include "bvh_prefix_dxil.h"
#include "bvh_radix_alloc_dxil.h"

enum bvh_kind { BVH_INIT, BVH_OFFLINE, BVH_PARTITIONS, BVH_RESET_COUNTERS, BVH_INSTANCES, BVH_RESET_INDEX, BVH_COPY_UPDATE, BVH_COPY_COUNTERS, BVH_LOAD_INSTANCES, BVH_BEGIN_PASS, BVH_HEADERS, BVH_MORTON32, BVH_MORTON64,
    BVH_BITONIC_PRE, BVH_BITONIC_OUTER, BVH_BITONIC_INNER, BVH_BITONIC_PRE64, BVH_BITONIC_OUTER64, BVH_BITONIC_INNER64,
    BVH_RADIX_RESET, BVH_RADIX_COUNT, BVH_RADIX_ALLOC,
    BVH_PREFIX64, BVH_PREFIX128, BVH_PREFIX256, BVH_PREFIX512, BVH_PREFIX1024, BVH_PREFIX2048, BVH_PREFIX4096,
    BVH_REARRANGE_TRIANGLES, BVH_REARRANGE_BVHS, BVH_LOAD_PASS, BVH_SPLIT_PASS,
    BVH_LOAD_TRIANGLES, BVH_RELOAD_TRIANGLES, BVH_LOAD_PROCEDURAL, BVH_RELOAD_PROCEDURAL,
    BVH_PARTITION_CONSTRUCT, BVH_CONSTRUCT, BVH_REFIT, BVH_SPLITS32, BVH_SPLITS64, BVH_POSTBUILD, BVH_COPY_COMPACT, BVH_FIND_TREELETS,
    BVH_RESET_KDOP, BVH_BUILD_KDOP, BVH_APEX_MAP };
static const struct bvh_candidate {
    SIZE_T bytes;
    BYTE digest[32];
    const WCHAR *name;
    UINT constants;
    D3D12_SHADER_BYTECODE cs;
} bvh_candidates[]={
    [BVH_POSTBUILD]={0x14a4,{0x7c,0x93,0x75,0xfc,0x0c,0x72,0xf7,0xac,0x09,0xed,0xa6,0xd9,0x6b,0x15,0x64,0x52,
        0xff,0x9b,0x16,0x76,0x97,0xaa,0xfa,0xd5,0x24,0xd3,0x6a,0xbb,0xf4,0xee,0x66,0xa1},L"XDXR GetBVHCompactedSize",4,{0}},
    [BVH_PARTITION_CONSTRUCT]={0x5e60,{0x9a,0x43,0x95,0xf1,0x51,0xa8,0xb7,0x95,0xf3,0xed,0xc0,0xce,0x3c,0x0c,0x6b,0x13,
        0x88,0x05,0x7f,0x96,0xf7,0x1e,0x7d,0x9a,0x7f,0x43,0xf5,0x1f,0xcb,0x26,0x50,0xab},L"XDXR PartitionConstruct",3,{0}},
    [BVH_CONSTRUCT]={0x7160,{0x3c,0x12,0x64,0x75,0x69,0x3c,0xf4,0x7f,0x83,0x89,0xb7,0xf1,0xa1,0xae,0x0f,0xdd,
        0x99,0xa3,0xe7,0x23,0x71,0xa4,0xee,0x92,0x38,0x59,0x68,0xee,0x39,0x9b,0x20,0xe3},L"XDXR Construct",3,{0}},
    [BVH_REFIT]={0x84e4,{0xc3,0x28,0xd0,0x23,0xa7,0xc4,0x10,0xee,0x90,0x91,0x04,0x5e,0xf0,0x03,0xde,0x8c,
        0xde,0xed,0x07,0x9c,0x93,0x69,0x30,0x19,0xc1,0xdb,0x57,0x90,0x35,0x18,0x32,0xbe},L"XDXR Refit",3,{0}},
    [BVH_FIND_TREELETS]={0x4dd8,{0x25,0x30,0x20,0xb5,0x58,0xe8,0xc5,0x46,0xa9,0x70,0xa0,0xf3,0x16,0x3d,0x98,0x76,
        0x43,0x59,0x50,0xa5,0x9b,0x44,0x03,0x3c,0x1a,0xbb,0xc3,0x0e,0x57,0x6a,0x5a,0x56},L"XDXR FindTreelets",5,{0}},
    [BVH_RADIX_RESET]={0xfec,{0x7a,0xad,0x87,0xa0,0x35,0x08,0x88,0x93,0x10,0x13,0x3d,0xea,0xdd,0x31,0xd9,0xe2,
        0x76,0x4a,0x3a,0x66,0x59,0xa2,0xa4,0x5e,0x72,0xd5,0xc2,0x80,0x00,0xb5,0xf9,0xc8},L"XDXR RadixReset",3,{0}},
    [BVH_RADIX_COUNT]={0x1ffc,{0x6a,0x34,0x25,0x42,0x28,0x73,0xcb,0x21,0x86,0x21,0x4f,0x59,0x36,0xe3,0x0b,0x32,
        0x48,0x8d,0xba,0x94,0x47,0x7b,0x30,0x57,0xf2,0xca,0x4d,0x76,0xd3,0x1c,0x86,0x91},L"XDXR RadixSortCount",3,{0}},
    [BVH_RADIX_ALLOC]={0x3f38,{0x0d,0x76,0xeb,0x65,0x9d,0xaf,0xcf,0xa5,0x2e,0x8d,0x2f,0xed,0xb7,0x1e,0x33,0xa2,
        0xe9,0x95,0x9e,0xeb,0x1b,0x2f,0x3e,0x7e,0xc5,0x4a,0xb3,0x74,0xdc,0x4f,0xbb,0x30},L"XDXR RadixSortAlloc",3,
        {bvh_radix_alloc_dxil,sizeof bvh_radix_alloc_dxil}},
    [BVH_PREFIX64]={0x1360,{0x94,0xca,0xc3,0x29,0x8a,0x85,0x19,0x31,0xce,0xbe,0xdc,0x2c,0x09,0xd1,0xd2,0xd6,
        0x5a,0x9a,0x48,0xea,0x3f,0xdd,0x46,0x35,0x92,0x41,0x45,0x83,0x8a,0x20,0xca,0xc7},L"XDXR RadixSortPrefixSum64",1,{bvh_prefix64_dxil,sizeof bvh_prefix64_dxil}},
    [BVH_PREFIX128]={0x1380,{0xfa,0xd4,0x42,0x08,0x31,0xb2,0x29,0x16,0x50,0x28,0x15,0x85,0x79,0x4b,0x58,0xd7,
        0x01,0x51,0x87,0x24,0x16,0x84,0xb8,0x5f,0x6b,0x01,0x82,0x60,0xc5,0xe3,0xce,0xfd},L"XDXR RadixSortPrefixSum128",1,{bvh_prefix128_dxil,sizeof bvh_prefix128_dxil}},
    [BVH_PREFIX256]={0x13d0,{0x51,0xfe,0x64,0x42,0xd0,0x93,0xc0,0xec,0x2d,0x85,0x51,0x4b,0x64,0x82,0x9b,0x37,
        0xe7,0x1f,0x52,0x1a,0xd5,0x97,0x35,0x98,0x52,0xcb,0x6b,0x30,0x65,0xbd,0xc2,0xbc},L"XDXR RadixSortPrefixSum256",1,{bvh_prefix256_dxil,sizeof bvh_prefix256_dxil}},
    [BVH_PREFIX512]={0x1454,{0x54,0xa1,0x87,0x29,0x8d,0x8e,0xeb,0x6b,0xdb,0x1d,0x16,0x87,0xf9,0xd4,0xa4,0x32,
        0x83,0x9c,0xc7,0x25,0xd3,0x0a,0x61,0x61,0x55,0xb0,0x53,0x7c,0xac,0xbc,0x2c,0x04},L"XDXR RadixSortPrefixSum512",1,{bvh_prefix512_dxil,sizeof bvh_prefix512_dxil}},
    [BVH_PREFIX1024]={0x1644,{0x74,0xe6,0x3e,0x28,0x5f,0xf2,0x4c,0xc6,0xac,0x51,0xe8,0xb1,0xb3,0x40,0xef,0x3e,
        0x5c,0xde,0x90,0xb7,0xf5,0xf4,0x27,0xef,0x90,0x81,0xc0,0xeb,0x20,0xed,0xbe,0xb9},L"XDXR RadixSortPrefixSum1024",1,{bvh_prefix1024_dxil,sizeof bvh_prefix1024_dxil}},
    [BVH_PREFIX2048]={0x1644,{0x80,0xd7,0x69,0x22,0x8b,0xd8,0xfc,0xbb,0x87,0x1d,0x82,0x79,0xfe,0xb7,0x7a,0x7a,
        0xea,0x35,0x81,0x66,0xa8,0x37,0x25,0x5a,0xf6,0x31,0xdd,0x28,0xfe,0x08,0x82,0x53},L"XDXR RadixSortPrefixSum2048",1,{bvh_prefix2048_dxil,sizeof bvh_prefix2048_dxil}},
    [BVH_PREFIX4096]={0x16cc,{0xaf,0xb4,0x42,0x56,0xc7,0x40,0x07,0xcc,0xd6,0xd9,0xd3,0x65,0xf0,0xeb,0x1c,0x36,
        0x1b,0x1a,0xba,0xe3,0x16,0x6e,0xba,0x4e,0x31,0x03,0xd5,0x82,0x18,0x61,0x47,0x38},L"XDXR RadixSortPrefixSum4096",1,{bvh_prefix4096_dxil,sizeof bvh_prefix4096_dxil}},
    [BVH_REARRANGE_TRIANGLES]={0x1c80,{0xd2,0x91,0x26,0x59,0x3c,0xb2,0xd0,0x4e,0x6b,0x66,0x33,0x3c,0xb2,0x87,0x75,0x98,
        0x1e,0xb3,0xb8,0xed,0xeb,0xd3,0x8d,0x0d,0xc2,0xce,0xe9,0xaa,0xe4,0x89,0xa0,0x45},L"XDXR RearrangeTriangles",4,{0}},
    [BVH_REARRANGE_BVHS]={0x1794,{0x29,0x63,0x30,0x83,0x51,0x53,0x15,0xf9,0x22,0x33,0xce,0x4b,0x7c,0xf0,0x56,0xec,
        0x7a,0x0e,0x0a,0xdb,0xf0,0xdb,0xd5,0x70,0xe4,0x18,0xec,0xed,0x3c,0xa9,0xf8,0xf4},L"XDXR RearrangeBVHs",4,{0}},
    [BVH_RESET_KDOP]={0x10d0,{0xf4,0x0b,0xd5,0xe0,0x38,0xad,0x26,0x13,0x57,0x55,0x82,0xe1,0xaf,0x36,0xd0,0xa2,
        0x03,0x5f,0xdc,0x70,0x88,0x4a,0x71,0xe2,0xe2,0x6a,0xb3,0xd4,0x6e,0x34,0xc0,0x5c},L"XDXR ResetKDOP",6,{0}},
    [BVH_BUILD_KDOP]={0x1d5c,{0x5a,0x20,0xf1,0x3b,0x9d,0x18,0x0a,0x6f,0xc7,0x5f,0x5f,0xae,0xad,0x2c,0xae,0xd2,
        0x91,0x43,0x9d,0x71,0x5f,0x04,0xdb,0xce,0xb7,0xf8,0x97,0xea,0x29,0x79,0xf5,0x5d},L"XDXR BuildKDOP",6,{0}},
    [BVH_APEX_MAP]={0x1ce0,{0xa1,0xf7,0xd6,0x0c,0x9f,0x0a,0x73,0xaa,0xb8,0x52,0x3c,0xa3,0x4c,0xdb,0xa9,0x40,
        0xd6,0xd1,0xcd,0x87,0xda,0x9a,0xfa,0x28,0x2f,0x25,0x99,0xa8,0x05,0xbd,0xb4,0x47},L"XDXR BuildApexPointMap",6,{0}},
    [BVH_SPLITS32]={0x1e68,{0x8b,0x44,0x1b,0xd5,0x5f,0xfe,0x5a,0x4f,0x9e,0xa6,0xdd,0xa9,0x3a,0xc4,0x80,0x6a,
        0x8b,0xc8,0xab,0xf0,0x23,0xf4,0x76,0x2d,0xe7,0x93,0xad,0x1f,0x11,0x58,0xa8,0x73},L"XDXR BuildSplits 0",3,{0}},
    [BVH_SPLITS64]={0x2094,{0x30,0x3b,0xf3,0x55,0xc7,0x40,0x56,0x2c,0xaf,0x01,0xfa,0xea,0x84,0x35,0xa4,0x9c,
        0xfc,0xe0,0x34,0xa9,0x9c,0xa9,0xd7,0xc1,0x58,0x8d,0x2a,0x20,0x7d,0xb7,0xe1,0x70},L"XDXR BuildSplits 1",3,{0}},
    [BVH_LOAD_TRIANGLES]={0xa304,{0xb5,0x18,0x13,0xbc,0xda,0xb8,0x6c,0x83,0x04,0x27,0xa6,0xc3,0xc6,0x82,0x17,0x62,0x16,0xe0,0x01,0xf3,0x8e,0x7a,0x75,0xdd,0x49,0x80,0xe4,0xe8,0x57,0x6a,0xbf,0xdb},L"XDXR LoadTriangles",14,{0}},
    [BVH_RELOAD_TRIANGLES]={0xb1e4,{0xf6,0x29,0xa0,0xb0,0xfb,0xc0,0x71,0xc7,0xb5,0x86,0x0e,0x33,0xcf,0x07,0x46,0xd2,0xbe,0x6f,0xa4,0x08,0xc8,0x70,0x23,0xa2,0xda,0x54,0x57,0x97,0x6a,0x2c,0x94,0x23},L"XDXR ReloadTriangles",14,{0}},
    [BVH_LOAD_PROCEDURAL]={0x21b4,{0xd7,0x05,0x5e,0x99,0xf9,0x24,0x82,0x40,0x99,0xe2,0x4b,0x0f,0x31,0xa7,0xd9,0xc9,
        0x9f,0xa8,0xc6,0xee,0x03,0x26,0x84,0x56,0xad,0x5a,0xd2,0x02,0x81,0xa5,0x4f,0x80},L"XDXR LoadProceduralGeometry",14,{0}},
    [BVH_RELOAD_PROCEDURAL]={0x1bc8,{0xee,0x9e,0x9a,0xeb,0x01,0xd0,0xa2,0x95,0xe3,0xa4,0x8c,0x5f,0x86,0x2e,0x8c,0x9c,
        0x38,0x32,0x17,0x36,0x1f,0x7e,0xf6,0xb9,0xa3,0x16,0xee,0xc8,0x74,0x39,0xac,0x4f},L"XDXR ReloadProcedurals",14,{0}},
    [BVH_SPLIT_PASS]={0x88f4,{0x5c,0x3d,0x3c,0x4a,0x41,0xf3,0x20,0xfb,0x73,0x50,0xd3,0xbf,0x23,0x84,0x95,0x50,
        0x78,0x6f,0xb8,0x9b,0x87,0x09,0xd6,0x25,0x12,0xf7,0x3e,0xad,0xf2,0x08,0x8e,0xee},L"XDXR SplitInstancesPass",8,{0}},
    [BVH_LOAD_PASS]={0x1230c,{0xd4,0x8a,0xbe,0x6c,0x23,0x4f,0xae,0x56,0xa4,0xbb,0x39,0x80,0xcd,0x83,0x98,0xa5,0x4a,0xf3,0x0f,0x8b,0x58,0xda,0xbc,0xb9,0x0c,0x60,0x07,0x7b,0x6c,0x4a,0xa8,0xed},L"XDXR LoadInstancesPass",10,{0}},
    [BVH_HEADERS]={0x1488,{0x7b,0xdd,0x8b,0x5b,0xf4,0x76,0xe7,0xf2,0x09,0x51,0xa9,0x2c,0x55,0x6d,0x6f,0x39,
        0x4d,0x4e,0xf9,0xbd,0x4c,0xa3,0x39,0x69,0x69,0x29,0x8d,0x3e,0xe1,0xd8,0xdd,0x7d},L"XDXR InitHeaders",1,{0}},
    [BVH_BEGIN_PASS]={0x1118,{0x9b,0xa6,0x1b,0x8f,0x5b,0x27,0x54,0x1f,0x62,0xe4,0x92,0x2d,0x1c,0x97,0xa2,0x0c,
        0xef,0x3c,0x5d,0x26,0xf4,0xef,0xb2,0x96,0x30,0x8e,0x32,0x70,0x07,0xa6,0xb4,0x39},L"XDXR BeginPass",5,{NULL,0}},
    [BVH_BITONIC_PRE]={0x20c8,{0x5b,0xc2,0x4d,0xd2,0xf2,0x93,0x64,0x76,0xc0,0xe7,0xc8,0x04,0x46,0xa9,0x97,0x95,
        0xdf,0x05,0xa7,0xc7,0xcb,0x46,0xad,0xe3,0xc0,0xe8,0x49,0x67,0xa5,0x64,0x45,0x87},L"XDXR BitonicPreSort 0",2,{0}},
    [BVH_BITONIC_OUTER]={0x1468,{0xb2,0xe5,0x9c,0x48,0x6e,0x9e,0xb7,0x21,0x4b,0x10,0x47,0x17,0x18,0xc1,0x45,0x7e,
        0x9f,0x78,0x09,0x29,0xae,0x35,0xe5,0xc7,0x2c,0xf2,0x8a,0x8b,0x19,0x43,0x76,0x13},L"XDXR BitonicOuterSort 0",2,{0}},
    [BVH_BITONIC_INNER]={0x1f5c,{0xba,0x19,0x42,0x68,0xb4,0xe8,0xbf,0xff,0xe4,0x38,0xcd,0x47,0xe4,0x25,0x45,0x58,
        0xd2,0x2a,0xdc,0xcb,0xa4,0xd2,0x9c,0x96,0xb7,0x1f,0x99,0x0b,0xb3,0xaf,0x58,0x4d},L"XDXR BitonicInnerSort 0",2,{0}},
    [BVH_BITONIC_PRE64]={0x2204,{0x00,0x66,0x3d,0x62,0xd6,0x05,0x2e,0xbe,0x98,0x6e,0x1d,0x27,0xb4,0x0d,0xf9,0xf7,
        0x4c,0x16,0x04,0x01,0x88,0x76,0xcf,0xa7,0x24,0x29,0xd6,0x0d,0xe0,0x99,0xf9,0xa9},L"XDXR BitonicPreSort 1",2,{0}},
    [BVH_BITONIC_OUTER64]={0x1554,{0x12,0x62,0xd4,0x90,0x94,0xb2,0x8f,0xe9,0x68,0x69,0x88,0x23,0xff,0x23,0xcb,0x61,
        0xc4,0xbb,0x1e,0xa9,0x0c,0x13,0xd4,0x02,0xee,0x78,0x92,0xe1,0x83,0xde,0x95,0xb8},L"XDXR BitonicOuterSort 1",2,{0}},
    [BVH_BITONIC_INNER64]={0x2094,{0x72,0x8a,0x1f,0xa6,0x66,0x58,0x41,0xb9,0x9c,0x34,0xd6,0x46,0xb2,0xb1,0x18,0xe9,
        0x0b,0xee,0x76,0xa8,0x32,0xbb,0x10,0x5a,0x11,0xeb,0x90,0xe4,0x0b,0xed,0xe0,0x84},L"XDXR BitonicInnerSort 1",2,{0}},
    [BVH_INIT]={0x1410,{0xad,0xa7,0x13,0x02,0x58,0x9d,0xae,0x66,0x27,0xfb,0x5d,0xa6,0xd0,0xe7,0xc1,0x34,
        0x00,0x10,0x7c,0x6a,0xe5,0x6b,0xb1,0x5e,0x1d,0xca,0x58,0x48,0x79,0x23,0x23,0xad},
        L"XDXR InitPartitionedHeaders",8,{bvh_init_dxil,sizeof bvh_init_dxil}},
    [BVH_OFFLINE]={0x1ec4,{0xed,0x66,0x93,0xd0,0x74,0x05,0x32,0xcb,0x77,0xd3,0xb6,0xa1,0x6b,0x26,0x2e,0xfd,
        0x01,0x26,0xa7,0x2b,0xb4,0x4e,0x0a,0x96,0x70,0x25,0x36,0x61,0x4b,0x3c,0x00,0x3c},
        L"XDXR WriteOfflinePartition",40,{bvh_offline_dxil,sizeof bvh_offline_dxil}},
    [BVH_PARTITIONS]={0x1580,{0x2a,0xf6,0xb2,0xe1,0x61,0xe1,0xbe,0xa2,0xc9,0x3c,0x43,0xb4,0x0d,0xc7,0xb1,0x6f,
        0x77,0xb8,0x13,0x23,0x83,0x92,0x1f,0x31,0xa4,0xaf,0x0e,0xbd,0x34,0x5c,0x11,0x96},
        L"XDXR WritePartitions",36,{bvh_partitions_dxil,sizeof bvh_partitions_dxil}},
    [BVH_RESET_COUNTERS]={0xfe8,{0xe2,0x30,0x9a,0x6f,0x2c,0x95,0xd0,0x97,0xe5,0x00,0xd4,0xc3,0x8e,0x20,0x58,0x81,
        0xa0,0xc2,0x43,0x3f,0x17,0x54,0xf9,0x7b,0xae,0xf0,0x6e,0x17,0x5e,0xd2,0xb0,0x2e},
        L"XDXR ResetCounters",3,{NULL,0}},
    [BVH_RESET_INDEX]={0x1004,{0xaf,0xc3,0xab,0x7f,0x0a,0x51,0x29,0xc1,0x10,0x07,0x8a,0x8e,0x56,0xb3,0xb8,0x4f,
        0x85,0x91,0x2e,0xaa,0xe0,0xa9,0xba,0xb1,0x7e,0x8b,0x95,0x00,0xac,0x2f,0x4c,0xca},
        L"XDXR ResetToIndexBuffer",3,{NULL,0}},
    [BVH_INSTANCES]={0x493c,{0xda,0x1d,0x4b,0xc3,0xb8,0xc1,0xf6,0x85,0x19,0x76,0xd8,0x99,0x18,0xba,0xb9,0x96,
        0x78,0xd1,0xb0,0xf7,0x4b,0x0c,0x91,0x8f,0xf1,0x58,0xe9,0xe9,0x20,0x54,0xb6,0x20},L"XDXR WriteInstances",16,{0}},
    [BVH_COPY_UPDATE]={0x126c,{0x96,0xfd,0xbb,0x6d,0x8c,0x64,0xad,0x8a,0x39,0x5f,0xc8,0x8f,0x28,0x4b,0xcd,0x48,
        0x3d,0xa7,0x71,0x94,0x37,0x6d,0xc5,0x29,0x07,0xed,0x93,0x1a,0x25,0x5e,0xbd,0x12},L"XDXR CopyForUpdate",2,{0}},
    [BVH_COPY_COMPACT]={0x1134,{0xad,0xa9,0x95,0x4c,0x4b,0x89,0xa8,0x7e,0x6b,0xe2,0x1a,0x1b,0x21,0x0f,0x72,0x4c,
        0xf3,0x12,0xef,0xfa,0xaa,0xe0,0x6a,0xd9,0x18,0x36,0x7e,0x43,0xbe,0x87,0xe5,0x05},L"XDXR CopyAndCompact",1,{0}},
    [BVH_LOAD_INSTANCES]={0x752c,{0xee,0x24,0xd3,0x76,0x1d,0xc2,0xfa,0x3a,0x74,0xf3,0x9e,0x97,0xfd,0x6b,0xf6,0xe8,
        0x68,0xf4,0x15,0x65,0xf6,0x0f,0x8b,0xdc,0x46,0x94,0x27,0x69,0xb5,0x5a,0x73,0x21},L"XDXR LoadInstances",16,{0}},
    [BVH_COPY_COUNTERS]={0x10e8,{0x1b,0x08,0x3c,0x49,0xc7,0x32,0xe1,0x85,0x49,0xcf,0xb7,0x53,0x78,0xe9,0x14,0xdb,
        0xca,0x80,0xb1,0xf1,0x22,0xc7,0x58,0xb3,0x55,0xc8,0xaa,0x3f,0x56,0xed,0x93,0x61},L"XDXR CopyCounters",2,{0}},
    [BVH_MORTON32]={0x33a0,{0xc3,0x42,0x0b,0x9e,0x30,0x8e,0x68,0xe9,0x17,0x22,0x14,0xef,0x4a,0x6d,0xa6,0xf9,
        0xb2,0x3f,0x34,0x9a,0xf6,0xcf,0x71,0x77,0x02,0x13,0x6f,0xd4,0x9f,0x4f,0xe8,0xff},L"XDXR CalculateMortonCodes 0",8,{0}},
    [BVH_MORTON64]={0x33fc,{0x94,0x16,0xd3,0x89,0x81,0x62,0xdb,0x0c,0x77,0x35,0xb0,0xb4,0x6f,0xb3,0xa8,0x0a,
        0xe7,0xdf,0x9e,0x6f,0x99,0x44,0x5b,0x74,0xca,0x32,0x20,0x77,0x08,0x16,0x46,0x1e},L"XDXR CalculateMortonCodes 1",8,{0}},
};

struct bvh_pipeline {
    IUnknown iface;
    LONG refs;
    /* +20760 copies this entire window. No native object belongs here. */
    BYTE inline_payload[32] __attribute__((aligned(8)));
    struct xbox_device *owner;
    ID3D12RootSignature *root; /* private translation layout */
    ID3D12PipelineState *host;
    enum bvh_kind kind;
};
_Static_assert(offsetof(struct bvh_pipeline,inline_payload)==0x10 &&
        offsetof(struct bvh_pipeline,owner)==0x30,"builder public PSO copy window");

/* Private reset-fixture key, not a hardware packet. Never recycle a serial. */
static BOOL bp_reset_key(BYTE payload[32])
{
    static LONG last;
    LONG n=InterlockedCompareExchange(&last,0,0);
    for (;;) {
        if (n==INT32_MAX) return FALSE;
        LONG seen=InterlockedCompareExchange(&last,n+1,n);
        if (seen==n) break;
        n=seen;
    }
    memcpy(payload,bvh_candidates[BVH_RESET_COUNTERS].digest,32);
    ++n; memcpy(payload,&n,sizeof n);
    return TRUE;
}
static ULONG WINAPI bp_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct bvh_pipeline,iface)->refs);
}
static ULONG WINAPI bp_release(IUnknown *i)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    ULONG refs=InterlockedDecrement(&p->refs);
    if (!refs) {
        ID3D12PipelineState_Release(p->host);
        ID3D12RootSignature_Release(p->root);
        IUnknown_Release(&p->owner->iface);
        HeapFree(GetProcessHeap(),0,p);
    }
    return refs;
}
static HRESULT WINAPI bp_query(IUnknown *i,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_IUnknown) && !IsEqualIID(&id,&IID_ID3D12PipelineState)) return E_NOINTERFACE;
    bp_addref(i); *out=i;
    return S_OK;
}
static HRESULT WINAPI bp_name(IUnknown *i,const WCHAR *name)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    const WCHAR *expected=bvh_candidates[p->kind].name;
    WCHAR s[32]; SIZE_T bytes=(wcslen(expected)+1)*sizeof(WCHAR);
    if (!copy_in(s,name,bytes)) return E_POINTER;
    if (memcmp(s,expected,bytes)) return E_NOTIMPL;
    return ID3D12PipelineState_SetName(p->host,expected);
}
static const struct bp_methods {
    IUnknownVtbl unknown;
    xs_stop_fn gap18[3];
    __typeof__(&bp_name) name;
    xs_stop_fn get_device, cached_blob;
} bp_methods={{bp_query,bp_addref,bp_release},{xs_stop,xs_stop,xs_stop},bp_name,xs_stop,xs_stop};
_Static_assert(offsetof(struct bp_methods,name)==0x30,"PSO name slot");

static HRESULT WINAPI bootstrap_bvh_pipeline(IUnknown *device,const void *desc,UINT64 x,UINT64 y,REFIID iid,void **out)
{
    struct { IUnknown *root; const void *shader; SIZE_T n; BYTE tail[32]; } w;
    _Static_assert(sizeof w==0x38,"console +1f0 descriptor");
    if (!out) return E_POINTER;
    *out=NULL;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12PipelineState)) return E_NOINTERFACE;
    if (x || y) return E_NOTIMPL;
    if (!copy_in(&w,desc,sizeof w)) return E_POINTER;
    BYTE code[0x1230c];
    enum bvh_kind kind=BVH_INIT;
    while (kind<ARRAYSIZE(bvh_candidates) && w.n!=bvh_candidates[kind].bytes) ++kind;
    if (kind==ARRAYSIZE(bvh_candidates) || w.n>sizeof code || memcmp(w.tail,(BYTE[32]){0},sizeof w.tail)) return E_NOTIMPL;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    if (!copy_in(code,w.shader,w.n)) return E_POINTER;
    /* Size bounds the copy. The digest, not the size, selects the kernel and its root. */
    while (kind<ARRAYSIZE(bvh_candidates) &&
            (w.n!=bvh_candidates[kind].bytes || !shader_digest_matches(code,w.n,bvh_candidates[kind].digest))) ++kind;
    if (kind==ARRAYSIZE(bvh_candidates)) return E_NOTIMPL;
    const struct bvh_candidate *candidate=&bvh_candidates[kind];
    BOOL morton=kind==BVH_MORTON32 || kind==BVH_MORTON64;
    BOOL bitonic=kind>=BVH_BITONIC_PRE && kind<=BVH_BITONIC_INNER64;
    BOOL prefix=kind>=BVH_PREFIX64 && kind<=BVH_PREFIX4096;
    BOOL original_radix=kind==BVH_RADIX_RESET || kind==BVH_RADIX_COUNT;
    BOOL rearrange=kind==BVH_REARRANGE_TRIANGLES || kind==BVH_REARRANGE_BVHS;
    BOOL triangles=kind==BVH_LOAD_TRIANGLES || kind==BVH_RELOAD_TRIANGLES;
    BOOL procedural=kind==BVH_LOAD_PROCEDURAL || kind==BVH_RELOAD_PROCEDURAL;
    BOOL builder=kind>=BVH_PARTITION_CONSTRUCT && kind<=BVH_REFIT;
    BOOL splits=kind==BVH_SPLITS32 || kind==BVH_SPLITS64;
    BOOL apex=kind>=BVH_RESET_KDOP && kind<=BVH_APEX_MAP;
    int required_root=apex ? ROOT_APEX : kind==BVH_FIND_TREELETS ? ROOT_TREELET : kind==BVH_POSTBUILD ? ROOT_POSTBUILD : splits ? ROOT_HIERARCHY : builder ? ROOT_BVH4 : triangles || procedural ? ROOT_LOAD_PRIMITIVES :
            kind==BVH_SPLIT_PASS ? ROOT_SPLIT_INSTANCES : kind==BVH_LOAD_PASS ? ROOT_LOAD_INSTANCES :
            rearrange ? ROOT_REARRANGE : prefix ? ROOT_PREFIX : original_radix || kind==BVH_RADIX_ALLOC ? ROOT_RADIX :
            bitonic ? ROOT_BITONIC : morton ? ROOT_MORTON : kind==BVH_COPY_COMPACT ? ROOT_COPY : ROOT_UTILITY;
    AcquireSRWLockShared(&d->descriptor_lock);
    struct bvh_root *r=d->bvh_roots;
    while (r && &r->iface!=w.root) r=r->next;
    BOOL known=r && r->owner==d && (int)r->kind==required_root;
    ID3D12RootSignature *shared=known && (original_radix || kind==BVH_COPY_COMPACT) ? r->host : NULL;
    if (shared) ID3D12RootSignature_AddRef(shared);
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!known) return E_INVALIDARG;
    if (kind==BVH_RADIX_COUNT) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettRadixCountV1")) {
            ID3D12RootSignature_Release(shared); return E_NOTIMPL;
        }
    }

    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,1}};
    D3D12_ROOT_PARAMETER rp[7]={
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,.DescriptorTable={kind==BVH_COPY_COUNTERS ? 2u : 1u,ranges}},
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0,candidate->constants}}};
    D3D12_ROOT_SIGNATURE_DESC rs={2,rp,0,NULL,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if (kind==BVH_RADIX_ALLOC) ranges[0].NumDescriptors=6; /* Scatter requires bounded views. */
    if (kind==BVH_FIND_TREELETS) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedTreeletsV1")) return E_NOTIMPL;
        ranges[0].NumDescriptors=4; /* u4/u5 are unused in this kernel. */
        rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV};
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=4;
    }
    if (apex) {
        if (kind!=BVH_RESET_KDOP) {
            HMODULE core=GetModuleHandleW(L"d3d12core.dll");
            if (!core || !GetProcAddress(core,"ScarlettBoundedApexV1")) return E_NOTIMPL;
        }
        if (kind==BVH_BUILD_KDOP) {
            /* Mixed-width b0; separate private read and atomic windows. */
            rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV};
            rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,6}};
            rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
            rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
            rs.NumParameters=4;
        } else ranges[0].NumDescriptors=kind==BVH_RESET_KDOP ? 1 : 2;
    }
    if (builder) {
        /* Limit shader-selected nodes and indices to the supplied views. */
        ranges[0].NumDescriptors=kind==BVH_REFIT ? 2 : 8;
        if (kind!=BVH_PARTITION_CONSTRUCT) {
            HMODULE core=GetModuleHandleW(L"d3d12core.dll");
            if (!core || !GetProcAddress(core,"ScarlettBoundedBVH4V1")) return E_NOTIMPL;
        }
        if (kind==BVH_REFIT) {
            rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
            rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
            rs.NumParameters=4;
        }
    }
    if (prefix) {
        rp[2]=rp[1];
        for (UINT i=0;i<2;i++) rp[i]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={i,0}};
        rs.NumParameters=3;
    }
    D3D12_DESCRIPTOR_RANGE proc_views[]={
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,4,0,2}};
    if (triangles || procedural || kind==BVH_SPLIT_PASS) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        const char *marker=triangles ? "ScarlettBoundedTrianglesV1" : procedural ? "ScarlettBoundedProceduralsV1" : "ScarlettBoundedSplitPassV1";
        if (!core || !GetProcAddress(core,marker)) return E_NOTIMPL;
        if (triangles) {
            ranges[0].NumDescriptors=5; ranges[1].NumDescriptors=3; ranges[1].OffsetInDescriptorsFromTableStart=5;
            rp[0].DescriptorTable.NumDescriptorRanges=2;
        } else if (procedural) {
            rp[0].DescriptorTable=(D3D12_ROOT_DESCRIPTOR_TABLE){3,proc_views};
            rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV};
        } else ranges[0].NumDescriptors=4;
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,6}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rs.NumParameters=5;
    }
    if (kind==BVH_LOAD_PASS) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedLoadPassV1")) return E_NOTIMPL;
        for (UINT i=0;i<3;i++) rp[i]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={i,0}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0,10}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,6}};
        rp[5]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rp[6]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rs.NumParameters=7;
    }
    if (rearrange || splits || kind==BVH_POSTBUILD) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        const char *marker=splits ? "ScarlettBoundedHierarchyV1" : rearrange ? "ScarlettBoundedRearrangeV1" : "ScarlettBoundedPostbuildV1";
        if (!core || !GetProcAddress(core,marker)) return E_NOTIMPL;
        ranges[0].NumDescriptors=rearrange ? 7 : 3;
        /* b0.xy is also loaded as i64: retain its byte representation in a CBV. */
        rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV};
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=4;
    }
    if (kind==BVH_HEADERS) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedInitHeadersV1")) return E_NOTIMPL;
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rs.NumParameters=4;
    }
    if (kind==BVH_BEGIN_PASS) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedBeginPassV1")) return E_NOTIMPL;
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,12}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={1,0x5358}};
        rs.NumParameters=5;
    }
    if (bitonic) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedBitonicV1")) return E_NOTIMPL;
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={1,0,2}};
        for (UINT i=0;i<2;i++) rp[2+i]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={i,0}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[5]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=6;
    }
    if (kind==BVH_COPY_UPDATE) {
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rs.NumParameters=3;
    }
    if (morton) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedMortonV1")) return E_NOTIMPL;
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV};
        for (UINT i=1;i<=4;i++) rp[i]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={i-1,0}};
        rp[5]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[6]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=7;
    }
    if (kind==BVH_INSTANCES) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedInstanceReadsV1")) return E_NOTIMPL;
        rp[2]=rp[1];
        rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=5;
    }
    if (kind==BVH_LOAD_INSTANCES) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedLoadInstancesV1")) return E_NOTIMPL;
        rp[2]=rp[1];
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,6}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rp[5]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rs.NumParameters=6;
    }
    struct bvh_pipeline *p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *p);
    if (!p) { if (shared) ID3D12RootSignature_Release(shared); return E_OUTOFMEMORY; }
    if (kind==BVH_RESET_COUNTERS && !bp_reset_key(p->inline_payload)) {
        HeapFree(GetProcessHeap(),0,p); return E_OUTOFMEMORY;
    }
    HRESULT hr=S_OK;
    p->root=shared;
    if (!shared) {
        ID3DBlob *blob=NULL;
        hr=D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,NULL);
        if (SUCCEEDED(hr)) {
            hr=ID3D12Device_CreateRootSignature(d->host,1,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),
                    &IID_ID3D12RootSignature,(void **)&p->root);
            ID3D10Blob_Release(blob);
        }
    }
    if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd={.pRootSignature=p->root,.CS=candidate->cs};
        if (!candidate->cs.pShaderBytecode) pd.CS=(D3D12_SHADER_BYTECODE){code,w.n};
        hr=ID3D12Device_CreateComputePipelineState(d->host,&pd,&IID_ID3D12PipelineState,(void **)&p->host);
    }
    if (FAILED(hr)) {
        if (p->root) ID3D12RootSignature_Release(p->root);
        HeapFree(GetProcessHeap(),0,p);
        return hr;
    }
    IUnknown_AddRef(device); p->owner=d; p->refs=1; p->iface.lpVtbl=&bp_methods.unknown; p->kind=kind;
    *out=&p->iface;
    return S_OK;
}

struct xs_bvh_hold { IUnknown *pipeline; ID3D12Resource *dst,*upload,*extra; ID3D12DescriptorHeap *heap; };
void xs_drop_bvh_hold(struct xs_bvh_hold *h)
{
    if (!h) return;
    if (h->upload) ID3D12Resource_Release(h->upload);
    if (h->extra) ID3D12Resource_Release(h->extra);
    if (h->heap) ID3D12DescriptorHeap_Release(h->heap);
    ID3D12Resource_Release(h->dst); IUnknown_Release(h->pipeline);
    HeapFree(GetProcessHeap(),0,h);
}
static HRESULT bvh_view_offset(ID3D12Device *device,const struct xs_bvh_range *range,UINT64 addr,UINT size,BOOL uav,UINT64 *offset)
{
    if (!range || !range->resource) return E_INVALIDARG;
    ID3D12Device *other;
    HRESULT hr=ID3D12Resource_GetDevice(range->resource,&IID_ID3D12Device,(void **)&other);
    if (FAILED(hr)) return hr;
    BOOL match=other==device; ID3D12Device_Release(other);
    if (!match) return E_INVALIDARG;
    D3D12_RESOURCE_DESC rd; range->resource->lpVtbl->GetDesc(range->resource,&rd);
    UINT64 delta=addr-range->guest_base;
    if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER ||
        (uav ? !(rd.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) : (rd.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) ||
        !range->guest_base || range->bytes>UINT64_MAX-range->guest_base || addr<range->guest_base ||
        (addr&3) || range->resource_offset>rd.Width || range->bytes>rd.Width-range->resource_offset ||
        delta>range->bytes || size>range->bytes-delta) return E_INVALIDARG;
    *offset=range->resource_offset+delta;
    return *offset%D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT ? E_INVALIDARG : S_OK;
}
static HRESULT bvh_list_device(IUnknown *pipeline,enum bvh_kind kind,ID3D12GraphicsCommandList *list,ID3D12Device **out)
{
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown || !list) return E_INVALIDARG;
    if (CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface)->kind!=kind) return E_INVALIDARG;
    if (ID3D12GraphicsCommandList_GetType(list)!=D3D12_COMMAND_LIST_TYPE_COMPUTE) return E_INVALIDARG;
    ID3D12Device *device=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface)->owner->host;
    ID3D12Device *other=NULL;
    HRESULT hr=ID3D12GraphicsCommandList_GetDevice(list,&IID_ID3D12Device,(void **)&other);
    if (FAILED(hr)) return hr;
    BOOL match=other==device; ID3D12Device_Release(other);
    if (!match) return E_INVALIDARG;
    *out=device;
    return S_OK;
}
static HRESULT bvh_record_buffers(IUnknown *pipeline,enum bvh_kind kind,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *range,UINT64 addr,UINT size,const struct xs_bvh_range *src,struct xs_bvh_hold **out)
{
    if (!range || !range->resource) return E_INVALIDARG;
    ID3D12Device *device=NULL;
    HRESULT hr=bvh_list_device(pipeline,kind,list,&device);
    if (FAILED(hr)) return hr;
    ID3D12Resource *dst=range->resource;
    UINT64 first,src_first=0;
    hr=bvh_view_offset(device,range,addr,size,TRUE,&first);
    if (FAILED(hr)) return hr;
    if (src) {
        if (src->resource==dst) return E_INVALIDARG;
        hr=bvh_view_offset(device,src,src->guest_base,size,FALSE,&src_first);
        if (FAILED(hr)) return hr;
    }
    struct xs_bvh_hold *h=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *h);
    if (!h) return E_OUTOFMEMORY;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,src ? 2u : 1u,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    hr=ID3D12Device_CreateDescriptorHeap(device,&hd,&IID_ID3D12DescriptorHeap,(void **)&h->heap);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,h); return hr; }
    h->dst=dst; ID3D12Resource_AddRef(dst); h->pipeline=pipeline; IUnknown_AddRef(pipeline);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    h->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(h->heap,&cpu);
    h->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(h->heap,&gpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC view={.Format=DXGI_FORMAT_R32_TYPELESS,.ViewDimension=D3D12_UAV_DIMENSION_BUFFER,
            .Buffer={.FirstElement=first/4,.NumElements=size/4,.Flags=D3D12_BUFFER_UAV_FLAG_RAW}};
    ID3D12Device_CreateUnorderedAccessView(device,dst,NULL,&view,cpu);
    if (src) {
        cpu.ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,hd.Type);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv={.Format=DXGI_FORMAT_R32_TYPELESS,.ViewDimension=D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer={.FirstElement=src_first/4,.NumElements=size/4,.Flags=D3D12_BUFFER_SRV_FLAG_RAW}};
        ID3D12Device_CreateShaderResourceView(device,src->resource,&srv,cpu);
        h->upload=src->resource; ID3D12Resource_AddRef(h->upload);
    }
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&h->heap);
    ID3D12GraphicsCommandList_SetPipelineState(list,p->host);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,p->root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,gpu);
    *out=h;
    return S_OK;
}
static HRESULT bvh_record_target(IUnknown *pipeline,enum bvh_kind kind,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *range,UINT64 addr,UINT size,struct xs_bvh_hold **out)
{
    return bvh_record_buffers(pipeline,kind,list,range,addr,size,NULL,out);
}
HRESULT xs_record_bvh_copy_counters(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *dst,
        const struct xs_bvh_range *src,UINT count,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!dst || !src || !count) return E_INVALIDARG;
    if (count>32u*D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) return E_NOTIMPL;
    HRESULT hr=bvh_record_buffers(pipeline,BVH_COPY_COUNTERS,list,dst,dst->guest_base,16*count,src,out);
    if (FAILED(hr)) return hr;
    UINT constants[]={count,count};
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,2,constants,0);
    ID3D12GraphicsCommandList_Dispatch(list,(count+31)/32,1,1);
    return S_OK;
}
HRESULT xs_record_bvh_init(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *range,
        const void *initializer,SIZE_T bytes,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    struct { UINT64 addr; UINT words[8]; } init;
    _Static_assert(sizeof init==40,"PartitionedHeaderInitializer");
    if (bytes!=sizeof init) return E_INVALIDARG;
    if (!copy_in(&init,initializer,sizeof init)) return E_POINTER;
    const UINT n=init.words[5];
    if (n>65536) return E_NOTIMPL;
    HRESULT hr=bvh_record_target(pipeline,BVH_INIT,list,range,init.addr,n ? 84+28*(n-1) : 80,out);
    if (FAILED(hr)) return hr;
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,8,init.words,0);
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    return S_OK;
}
#include "bvh_offline_record.h"
#include "bvh_partitions_record.h"
#include "bvh_instances_record.h"
#include "bvh_copy_update_record.h"

HRESULT xs_record_bvh_reset(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *dst,UINT count,UINT value,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!dst || !count) return E_INVALIDARG;
    if (count>32u*D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) return E_NOTIMPL;
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown) return E_INVALIDARG;
    enum bvh_kind kind=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface)->kind;
    if (kind!=BVH_RESET_COUNTERS && kind!=BVH_RESET_INDEX) return E_INVALIDARG;
    HRESULT hr=bvh_record_target(pipeline,kind,list,dst,dst->guest_base,16*count,out);
    if (SUCCEEDED(hr)) {
        UINT constants[]={count,count,value};
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,3,constants,0);
        ID3D12GraphicsCommandList_Dispatch(list,(count+31)/32,1,1);
    }
    return hr;
}

#include "bvh_beginpass_record.h"
#include "bvh_headers_record.h"
#include "bvh_morton_record.h"
