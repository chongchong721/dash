#ifndef Linear_H
#define Linear_H

#include <cstring>
#include <cmath>
#include <vector>
#include <iostream>
#include <cmath>
#include <thread>
#include <shared_mutex>
#include <bitset>
#include <cassert>
#include <unordered_map>
#include "../util/hash.h"
#include "../util/pair.h"
#include "../util/persist.h"
#include <immintrin.h>
#include "allocator.h"
#define _INVALID 0 /* we use 0 as the invalid key*/ 
#define SINGLE 1
#define DOUBLE_EXPANSION 1

#ifdef PMEM
#include <libpmemobj.h>
#endif

#define SIMD 1
#define SIMD_CMP8(src, key)                                                        \
  do                                                                               \
  {                                                                                \
    const __m256i key_data = _mm256_set1_epi8(key);                                \
    __m256i seg_data = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src)); \
    __m256i rv_mask = _mm256_cmpeq_epi8(seg_data, key_data);                       \
    mask = _mm256_movemask_epi8(rv_mask);                                          \
  } while (0)

#define SSE_CMP8(src, key)                                                         \
  do                                                                               \
  {                                                                                \
    const __m128i key_data = _mm_set1_epi8(key);                                   \
    __m128i seg_data = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));    \
    __m128i rv_mask = _mm_cmpeq_epi8(seg_data, key_data);                          \
    mask = _mm_movemask_epi8(rv_mask);                                            \
  } while (0)

#define CHECK_BIT(var, pos) ((((var) & (1<<(pos))) > 0) ? (1) : (0))

const uint32_t lockSet = 1 << 31; /*locking information*/
const uint32_t lockMask = ((uint32_t)1 << 31) - 1; /*locking mask*/
const int overflowSet = 1 << 15;
const int countMask = (1 << 4) - 1;
const uint32_t initialSet = 1 << 30; 
const uint32_t initialLockSet = lockSet | initialSet;
const uint32_t versionMask = (1 << 30) -1;
uint64_t overflow_access;

template<class T>
struct _Pair{
  T key;
  Value_t value;
};

constexpr size_t k_PairSize = 16; 
const size_t kNumPairPerBucket = 14; 
const size_t kFingerBits = 8;
constexpr size_t kMask = (1 << kFingerBits) -1;
const uint32_t kNumBucket = 64;
const uint32_t stashBucket = 2;
const uint32_t fixedExpandNum = 8;
const uint32_t fixedExpandBits = 31 - __builtin_clz(fixedExpandNum);
const uint32_t fixedExpandMask = (1 << fixedExpandBits) -1;
constexpr int allocMask = (1 << kNumPairPerBucket) - 1;
//constexpr size_t bucketMask = ((1 << (int)log2(kNumBucket)) - 1);
constexpr size_t bucketMask = ((1 << (31 - __builtin_clz(kNumBucket))) - 1);
//constexpr size_t stashMask = (1 << (int)log2(stashBucket)) -1;
constexpr size_t stashMask = (1 << (31 - __builtin_clz(stashBucket))) -1;
constexpr uint8_t stashHighMask = ~((uint8_t)stashMask);
constexpr uint32_t segmentSize = 256;
//constexpr size_t baseShifBits = static_cast<uint64_t>(log2(segmentSize));
constexpr size_t baseShifBits = static_cast<uint64_t>(31 - __builtin_clz(segmentSize));
constexpr uint32_t segmentMask = (1 << baseShifBits) - 1; 
constexpr size_t directorySize = 1024*2;
const uint64_t low32Mask = ((uint64_t)1 << 32) - 1;
const uint64_t high32Mask = ~low32Mask;
const uint8_t low2Mask = (1 << 2) - 1;
const uint32_t low4Mask = (1 << 4) - 1;
//constexpr size_t shiftBits = (size_t)log2(kNumBucket) + kFingerBits;
constexpr uint32_t shiftBits = (31 - __builtin_clz(kNumBucket)) + kFingerBits;
const uint32_t partitionNum = 1;
constexpr uint64_t partitionMask = ((1 << (31 - __builtin_clz(partitionNum))) - 1);
constexpr uint32_t partitionShifBits = shiftBits + (31 - __builtin_clz(partitionNum));
constexpr uint32_t expandShiftBits = fixedExpandBits + baseShifBits;

#define PARTITION_INDEX(hash) (((hash) >> (64 - partitionShifBits)) & partitionMask)
#define BUCKET_INDEX(hash) (((hash) >> (64 - shiftBits)) & bucketMask)
#define META_HASH(hash) ((uint8_t)((hash) >> (64 - kFingerBits)))
#define GET_COUNT(var) ((var) & countMask)
#define GET_BITMAP(var) (((var) >> 4) & allocMask)
#define ORG_BITMAP(var) ((~((var) & allocMask)) & allocMask)
#define PROBE_BITMAP(var) ((var) & allocMask)

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#define PARTITION 1

const int tab32[32] = {
     0,  9,  1, 10, 13, 21,  2, 29,
    11, 14, 16, 18, 22, 25,  3, 30,
     8, 12, 20, 28, 15, 17, 24,  7,
    19, 27, 23,  6, 26,  5,  4, 31};

inline int log2_32 (uint32_t value)
{
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return tab32[(uint32_t)(value*0x07C4ACDD) >> 27];
}

inline uint32_t pow2(int shift_index){
	//assert(shift_index < 32);
	return (1<<shift_index);
}

inline uint64_t IDX(uint64_t hashKey, uint64_t level){
	return hashKey & ((1 << level) - 1);
}

inline void SEG_IDX_OFFSET(uint32_t bucket_idx, uint32_t& seg_idx, uint32_t& seg_offset){
	#ifdef DOUBLE_EXPANSION
		//seg_idx = 31 - __builtin_clz((bucket_idx >> (baseShifBits))*2+1);
		//seg_offset = bucket_idx - ((seg_idx == 0)?0:(pow2(seg_idx-1)*segmentSize));
		int index = 31 - __builtin_clz((bucket_idx >> expandShiftBits)+1);
		int shift = ((bucket_idx >> baseShifBits) - (fixedExpandNum*(pow2(index) -1))) >> index;
        seg_idx = fixedExpandNum*index + shift;
        seg_offset = bucket_idx - segmentSize*((pow2(index)-1)*fixedExpandNum + pow2(index)*shift);
	#else
		seg_idx = bucket_idx >> baseShifBits;
		seg_offset = bucket_idx & segmentMask;
	#endif
}

uint64_t SUM_BUCKET(uint32_t bucket_idx){
	int index = 31 - __builtin_clz((bucket_idx >> expandShiftBits)+1);
	int shift = (((bucket_idx >> baseShifBits) - (fixedExpandNum*(pow2(index) -1))) >> index);
	uint64_t sum = segmentSize*((pow2(index)-1)*fixedExpandNum + pow2(index)*(shift+1));
	return sum;
}

inline uint32_t SEG_SIZE(uint32_t bucket_ix){
	int index = 31 - __builtin_clz((bucket_ix >> expandShiftBits)+1);
	return segmentSize*pow2(index);
}

/*the size uses 8 byte as the uinit*/
template<class T>
struct overflowBucket{
	overflowBucket() {
		memset(this, 0, sizeof(struct overflowBucket));
		//memset(this, 0 ,64);
		//initialize(this, 8);
	}
	
	inline int find_empty_slot(){
		if (GET_COUNT(bitmap) == kNumPairPerBucket)
		{
			return -1;
		}
		auto mask = ~(GET_BITMAP(bitmap)); //Now the 1 bit should be the empty slot
		return __builtin_ctz(mask);
	}

	Value_t check_and_get(uint8_t meta_hash, T key){
		int mask = 0;
  		SSE_CMP8(finger_array, meta_hash);
  		mask = mask & GET_BITMAP(bitmap);

  		if constexpr (std::is_pointer_v<T>){
			/*loop unrolling*/
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (strcmp(_[i].key, key) == 0)) {
						return _[i].value;
					}

					if (CHECK_BIT(mask, i + 1) && (strcmp(_[i + 1].key, key) == 0)) {
						return _[i + 1].value;
					}

					if (CHECK_BIT(mask, i + 2) && (strcmp(_[i + 2].key, key) == 0)) {
						return _[i + 2].value;
					}

					if (CHECK_BIT(mask, i + 3) && (strcmp(_[i + 3].key, key) == 0)) {
						return _[i + 3].value;
					}
				}

				if (CHECK_BIT(mask, 12) && (strcmp(_[12].key, key) == 0)) {
				return _[12].value;
				}

				if (CHECK_BIT(mask, 13) && (strcmp(_[13].key, key) == 0)) {
				return _[13].value;
				}
			}
		}else{
			/*loop unrolling*/
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (_[i].key == key)) {
						return _[i].value;
					}

					if (CHECK_BIT(mask, i + 1) && (_[i + 1].key == key)) {
						return _[i + 1].value;
					}

					if (CHECK_BIT(mask, i + 2) && (_[i + 2].key == key)) {
						return _[i + 2].value;
					}

					if (CHECK_BIT(mask, i + 3) && (_[i + 3].key == key)) {
						return _[i + 3].value;
					}
				}

				if (CHECK_BIT(mask, 12) && (_[12].key == key)) {
				return _[12].value;
				}

				if (CHECK_BIT(mask, 13) && (_[13].key == key)) {
				return _[13].value;
				}
			}
    	}
  		return NONE;
	}

	inline void set_hash(int index, uint8_t meta_hash) 
	{
	  finger_array[index] = meta_hash;
	  bitmap = bitmap | (1 << (index + 4));
	  assert(GET_COUNT(bitmap) < kNumPairPerBucket);
	  bitmap++;
	}

	inline void unset_hash(int index){
	  bitmap = bitmap & (~(1 << (index + 4)));
	  assert(GET_COUNT(bitmap) <= kNumPairPerBucket);
	  assert(GET_COUNT(bitmap) > 0);
	  bitmap--;
	}

	inline void get_lock(){
		auto old_value = version_lock & lockMask;
		auto new_value = version_lock | lockSet;
		while(!CAS(&version_lock, &old_value, new_value)){
			old_value = version_lock & lockMask;
			new_value = version_lock | lockSet;
		}
	}

	inline void release_lock(){
		auto old_value = version_lock;
		auto new_value = ((old_value & lockMask) + 1) & lockMask;

	    while(!CAS(&version_lock, &old_value, new_value)){
	      old_value = version_lock;
	      new_value = ((old_value & lockMask) + 1) & lockMask;
	    }
	}

	int Insert(T key, Value_t value, uint8_t meta_hash){
		auto slot = find_empty_slot();
		/* this branch can be optimized out*/
		assert(slot < kNumPairPerBucket);
		if (slot == -1)
		{
			//printf("cannot find the empty slot, for key %llu\n", key);
			return -1;
		}
		_[slot].value = value;
		_[slot].key = key;
#ifdef PMEM
        Allocator::Persist(&_[slot], sizeof(_[slot]));
#endif
		mfence();
		set_hash(slot, meta_hash);
		return 0;
	}

	int Delete(uint8_t meta_hash, T key){
		int mask = 0;
		SSE_CMP8(finger_array, meta_hash);
  		mask = mask & GET_BITMAP(bitmap);
  		/*loop unrolling*/
  		if constexpr (std::is_pointer_v<T>){
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (strcmp(_[i].key, key) == 0)) {
						unset_hash(i);
						return 0;
					}

					if (CHECK_BIT(mask, i + 1) && (strcmp(_[i + 1].key, key) == 0)) {
						unset_hash(i + 1);
						return 0;
					}

					if (CHECK_BIT(mask, i + 2) && (strcmp(_[i + 2].key, key) == 0)) {
						unset_hash(i + 2);
						return 0;
					}

					if (CHECK_BIT(mask, i + 3) && (strcmp(_[i + 3].key, key) == 0)) {
						unset_hash(i + 3);
						return 0;
					}
				}

				if (CHECK_BIT(mask, 12) && (strcmp(_[12].key, key) == 0)) {
					unset_hash(12);
					return 0;
				}

				if (CHECK_BIT(mask, 13) && (strcmp(_[13].key, key) == 0)) {
					unset_hash(13);
					return 0;
				}
			}
		}else{
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (_[i].key == key)) {
						unset_hash(i);
						return 0;
					}

					if (CHECK_BIT(mask, i + 1) && (_[i + 1].key == key)) {
						unset_hash(i + 1);
						return 0;
					}

					if (CHECK_BIT(mask, i + 2) && (_[i + 2].key == key)) {
						unset_hash(i + 2);
						return 0;
					}

					if (CHECK_BIT(mask, i + 3) && (_[i + 3].key == key)) {
						unset_hash(i + 3);
						return 0;
					}
				}

				if (CHECK_BIT(mask, 12) && (_[12].key == key)) {
					unset_hash(12);
					return 0;
				}

				if (CHECK_BIT(mask, 13) && (_[13].key == key)) {
					unset_hash(13);
					return 0;
				}
			}
    	}
  		return -1;
	}

	int Insert_with_noflush(T key, Value_t value, uint8_t meta_hash){
		auto slot = find_empty_slot();
		/* this branch can be optimized out*/
		assert(slot < kNumPairPerBucket);
		if (slot == -1)
		{
			//printf("cannot find the empty slot, for key %llu\n", key);
			return -1;
		}
		_[slot].value = value;
		_[slot].key = key;
		set_hash(slot, meta_hash);
		return 0;
	}
	
	uint32_t version_lock;
	int bitmap;
	uint8_t finger_array[kNumPairPerBucket+2];
	overflowBucket *next;
	_Pair<T> _[kNumPairPerBucket];
};

template<class T>
struct Bucket {
	inline int find_empty_slot(){
		if (GET_COUNT(bitmap) == kNumPairPerBucket)
		{
			return -1;
		}
		auto mask = ~(GET_BITMAP(bitmap)); //Now the 1 bit should be the empty slot
		return __builtin_ctz(mask);
	}

	/*true indicates overflow, needs extra check in the stash*/
	inline bool test_overflow(){
		return (overflowCount != 0)?true:false;
	}

	inline bool test_stash_check(){
		int mask = *((int *)membership);
		return ((mask & overflowSet) != 0)?true:false;
	}

	inline void clear_stash_check(){
		int mask = *((int *)membership);
		*((int *)membership) = (*((int *)membership)) & (~overflowSet);
	}

	inline void set_indicator(uint8_t meta_hash, Bucket<T> *neighbor, uint8_t pos){
		int mask = finger_array[14];
		mask = ~mask;
		auto index = __builtin_ctz(mask);

		if (index < 4)
		{
			finger_array[15+index] = meta_hash;
			finger_array[14] = ((uint8_t)(1 << index) | finger_array[14]);/*may be optimized*/
			finger_array[19] = (finger_array[19] & (~(3 << (index*2))))| (pos << (index * 2));			
		}else{
			mask = neighbor->finger_array[14];
			mask = ~mask;
			index = __builtin_ctz(mask);
			if (index < 4)
			{
				neighbor->finger_array[15+index] = meta_hash;
				neighbor->finger_array[14] = ((uint8_t)(1 << index) | neighbor->finger_array[14]);
				neighbor->overflowMember = ((uint8_t)(1 << index) | neighbor->overflowMember);
				neighbor->finger_array[19] = (neighbor->finger_array[19] & (~(3 << (index*2)))) | (pos << (index * 2));
			}else{/*overflow, increase count*/
				//printf("overflowcount is %d\n", overflowCount);
				assert(overflowCount < 255);
				overflowCount++;
			}
		}
		*((int *)membership) = (*((int *)membership)) | overflowSet;
	}

	/*both clear this bucket and its neighbor bucket*/
	inline bool unset_indicator(uint8_t meta_hash, Bucket<T> *neighbor, T key, uint64_t pos){
		/*also needs to ensure that this meta_hash must belongs to other bucket*/
		//printf("the meta hash is %d\n", meta_hash);
		bool clear_success = false;
		int mask1 = finger_array[14];
		for (int i = 0; i < 4; ++i)
		{
			if (CHECK_BIT(mask1, i) && (finger_array[15 + i] == meta_hash) && (((1 << i) & overflowMember) == 0) && (((finger_array[19] >> (2*i)) & low2Mask) == pos))
			{
				//printf("clear the indicator 1\n");
				finger_array[14] = finger_array[14] & ((uint8_t)(~(1 << i)));
				finger_array[19] = finger_array[19] & (~(3 << (i*2)));
				assert(((finger_array[19] >> (i*2)) & stashMask) == 0);
				clear_success = true;
				break;
			}
		}
		
		int mask2 = neighbor->finger_array[14];
		if (!clear_success)
		{
			for (int i = 0; i < 4; ++i)
			{
				if  (CHECK_BIT(mask2, i) && (neighbor->finger_array[15 + i] == meta_hash) && (((1 << i) & neighbor->overflowMember) != 0) && (((neighbor->finger_array[19] >> (2*i)) & low2Mask) == pos))
				{
					//printf("clear the indicator 2\n");
					neighbor->finger_array[14] = neighbor->finger_array[14] & ((uint8_t)(~(1 << i)));
					neighbor->overflowMember = neighbor->overflowMember & ((uint8_t)(~(1 << i)));
				 	neighbor->finger_array[19] = neighbor->finger_array[19] & (~(3 << (i*2)));
				 	assert(((neighbor->finger_array[19] >> (i*2)) & stashMask) == 0);
					clear_success = true;
					break;
				}
			}
		}

		if (!clear_success)
		{
			assert(overflowCount != 0);
			overflowCount--;
			//assert(overflowCount < 255);
		}

		mask1 = finger_array[14];
		mask2 = neighbor->finger_array[14];
		if (((mask1 & (~overflowMember)) == 0) && (overflowCount == 0) && ((mask2 & neighbor->overflowMember)==0))
		{
			//printf("clear the stash check\n");
			clear_stash_check();
		}
		//printf("success cleared for meta_hash %lld\n", meta_hash);
		return true;
	}

	int unique_check(uint8_t meta_hash, T key, Bucket<T>* neighbor, overflowBucket<T>* stash){
		//return check_and_get(meta_hash, kfey) == NONE ? 0 : -1;
		if ((check_and_get(meta_hash,key, false) != NONE) || (neighbor->check_and_get(meta_hash, key, true) != NONE))
		{
			return -1;
		}

		if (test_stash_check())
		{
			auto test_stash = false;
			if (test_overflow())
			{
				test_stash = true;
			}else{
				int mask = finger_array[14];
				if (finger_array[14] != 0)
				{
					for (int i = 0; i < 4; ++i)
					{
						if (CHECK_BIT(mask, i) && (finger_array[15+i] == meta_hash) && (((1 << i) & overflowMember) == 0))
						{
							test_stash = true;
							goto STASH_CHECK;
						}
					}
				}

				if (neighbor->finger_array[14] != 0)
				{
					mask = neighbor->finger_array[14];
					for (int i = 0; i < 4; ++i)
					{
						if (CHECK_BIT(mask, i) && (neighbor->finger_array[15+i] == meta_hash) && (((1 << i) & neighbor->overflowMember) != 0))
						{
							test_stash = true;
							break;
						}
					}	
				}
			}
		STASH_CHECK:
			if (test_stash)
			{
				for (int i = 0; i < stashBucket; ++i)
				{
					overflowBucket<T> *curr_bucket = stash + i;
					auto ret = curr_bucket->check_and_get(meta_hash, key);
					if (ret != NONE)
					{
					 	return -1;
					}
				}

				overflowBucket<T> *prev_bucket = stash;
				overflowBucket<T> *next_bucket = stash->next;
				while(next_bucket != NULL){
					auto ret = next_bucket->check_and_get(meta_hash, key);
					if (ret != NONE)
					{
						return -1;
					}
					prev_bucket = next_bucket;
					next_bucket = next_bucket->next;
				}
			}
		}
		return 0;
	}

	Value_t check_and_get(uint8_t meta_hash, T key, bool probe){
		int mask = 0;
  		SSE_CMP8(finger_array, meta_hash);
  		if (!probe)
  		{
  			mask = mask & GET_BITMAP(bitmap) & ((~(*(int*)membership)) & allocMask);
  		}else{
  			mask = mask & GET_BITMAP(bitmap) & ((*(int*)membership) & allocMask);
  		}

  		if constexpr (std::is_pointer_v<T>){
			/*loop unrolling*/
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (strcmp(_[i].key, key) == 0)) {
						return _[i].value;
					}

					if (CHECK_BIT(mask, i + 1) && (strcmp(_[i + 1].key, key) == 0)) {
						return _[i + 1].value;
					}

					if (CHECK_BIT(mask, i + 2) && (strcmp(_[i + 2].key, key) == 0)) {
						return _[i + 2].value;
					}

					if (CHECK_BIT(mask, i + 3) && (strcmp(_[i + 3].key, key) == 0)) {
						return _[i + 3].value;
					}
				}

				if (CHECK_BIT(mask, 12) && (strcmp(_[12].key, key) == 0)) {
				return _[12].value;
				}

				if (CHECK_BIT(mask, 13) && (strcmp(_[13].key, key) == 0)) {
				return _[13].value;
				}
			}
		}else{
			/*loop unrolling*/
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (_[i].key == key)) {
						return _[i].value;
					}

					if (CHECK_BIT(mask, i + 1) && (_[i + 1].key == key)) {
						return _[i + 1].value;
					}

					if (CHECK_BIT(mask, i + 2) && (_[i + 2].key == key)) {
						return _[i + 2].value;
					}

					if (CHECK_BIT(mask, i + 3) && (_[i + 3].key == key)) {
						return _[i + 3].value;
					}
				}

				if (CHECK_BIT(mask, 12) && (_[12].key == key)) {
				return _[12].value;
				}

				if (CHECK_BIT(mask, 13) && (_[13].key == key)) {
				return _[13].value;
				}
			}
    	}
  		return NONE;
	}

	inline void set_hash(int index, uint8_t meta_hash, bool probe) /* Do I needs the atomic instruction????*/
	{
	  finger_array[index] = meta_hash;
	  bitmap = bitmap | (1 << (index + 4));
	  assert(GET_COUNT(bitmap) < kNumPairPerBucket);
	  bitmap++;
	  if (probe)
	  {
	  	*((int *)membership) = (1 << index) | *((int *)membership);
	  }
	}

	inline uint8_t get_hash(int index){
	  return finger_array[index];
	}

	inline void unset_hash(int index){
	  bitmap = bitmap & (~(1 << (index + 4)));
	  assert(GET_COUNT(bitmap) <= kNumPairPerBucket);
	  assert(GET_COUNT(bitmap) > 0);
	  bitmap--;
	  *((int *)membership) = (~(1 << index)) & (*((int *)membership)); /*since they are in the same cacheline, therefore no performance influence?*/
	}

	/*for normal bucket*/
	inline void get_lock(){
		auto old_value = version_lock & lockMask;
		auto new_value = version_lock | lockSet; /*the first one bit is locked*/
		while(!CAS(&version_lock, &old_value, new_value)){
			old_value = version_lock & lockMask;
			new_value = version_lock | lockSet;
		}
	}

	inline bool try_get_lock(){
		auto old_value = version_lock & lockMask;
		auto new_value = version_lock | lockSet;
		return CAS(&version_lock, &old_value, new_value);
	}

	inline void release_lock(){
		auto old_value = version_lock;
		auto new_value = (((old_value & versionMask) + 1) & versionMask) | (old_value & initialSet);

	    while(!CAS(&version_lock, &old_value, new_value)){
	      old_value = version_lock;
	      new_value = new_value = (((old_value & versionMask) + 1) & versionMask) | (old_value & initialSet);
	    }
	}

	/*if the lock is set, return true*/
	inline bool test_lock_set(uint32_t& version){
	    auto value = __atomic_load_n(&version_lock, __ATOMIC_ACQUIRE);
	    version = value & versionMask;
	    return (value & lockSet) != 0;
	}

  // test whether the version has change, if change, return true
	inline bool test_lock_version_change(uint32_t old_version){
	    auto value = __atomic_load_n(&version_lock, __ATOMIC_ACQUIRE);
	    auto version = value & versionMask;
	    return ((value & lockSet) != 0) || (version != old_version); 
	}

	/*if it has been initialized, then return true*/
	inline bool test_initialize(){
		auto value = version_lock & initialSet;
		return (value != 0);
	}

	inline void set_initialize(){
		auto old_value = version_lock;
		auto new_value = version_lock | initialSet;
		while(!CAS(&version_lock, &old_value, new_value)){
			old_value = version_lock;
			new_value = version_lock | initialSet;
		}
	}

	int Insert(T key, Value_t value, uint8_t meta_hash, bool probe){
		auto slot = find_empty_slot();
		/* this branch can be optimized out*/
		assert(slot < kNumPairPerBucket);
		if (slot == -1)
		{
			//printf("cannot find the empty slot, for key %llu\n", key);
			return -1;
		}
		_[slot].value = value;
		_[slot].key = key;
#ifdef PMEM
        Allocator::Persist(&_[slot], sizeof(_[slot]));
#endif
        mfence();
        set_hash(slot, meta_hash, probe);
		return 0;
	}

	/*if delete success, then return 0, else return -1*/
	int Delete(uint8_t meta_hash, T key, bool probe){
		/*do the simd and check the key, then do the delete operation*/
		int mask = 0;
  		SSE_CMP8(finger_array, meta_hash);
  		if (!probe)
  		{
  			mask = mask & GET_BITMAP(bitmap) & ((~(*(int*)membership)) & allocMask);
  		}else{
  			mask = mask & GET_BITMAP(bitmap) & ((*(int*)membership) & allocMask);
  		}
  		/*loop unrolling*/
  		if constexpr (std::is_pointer_v<T>){
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (strcmp(_[i].key, key) == 0)) {
						unset_hash(i);
						return 0;
					}

					if (CHECK_BIT(mask, i + 1) && (strcmp(_[i + 1].key, key) == 0)) {
						unset_hash(i + 1);
						return 0;
					}

					if (CHECK_BIT(mask, i + 2) && (strcmp(_[i + 2].key, key) == 0)) {
						unset_hash(i + 2);
						return 0;
					}

					if (CHECK_BIT(mask, i + 3) && (strcmp(_[i + 3].key, key) == 0)) {
						unset_hash(i + 3);
						return 0;
					}
				}

				if (CHECK_BIT(mask, 12) && (strcmp(_[12].key, key) == 0)) {
					unset_hash(12);
					return 0;
				}

				if (CHECK_BIT(mask, 13) && (strcmp(_[13].key, key) == 0)) {
					unset_hash(13);
					return 0;
				}
			}
		}else{
			if (mask != 0) {
				for (int i = 0; i < 12; i += 4) {
					if (CHECK_BIT(mask, i) && (_[i].key == key)) {
						unset_hash(i);
						return 0;
					}

					if (CHECK_BIT(mask, i + 1) && (_[i + 1].key == key)) {
						unset_hash(i + 1);
						return 0;
					}

					if (CHECK_BIT(mask, i + 2) && (_[i + 2].key == key)) {
						unset_hash(i + 2);
						return 0;
					}

					if (CHECK_BIT(mask, i + 3) && (_[i + 3].key == key)) {
						unset_hash(i + 3);
						return 0;
					}
				}

				if (CHECK_BIT(mask, 12) && (_[12].key == key)) {
					unset_hash(12);
					return 0;
				}

				if (CHECK_BIT(mask, 13) && (_[13].key == key)) {
					unset_hash(13);
					return 0;
				}
			}
    	}
  		return -1;
	}

	int Insert_with_noflush(T key, Value_t value, uint8_t meta_hash, bool probe){
		auto slot = find_empty_slot();

		/* this branch can be optimized out*/
		assert(slot < kNumPairPerBucket);
		if (slot == -1)
		{
			//printf("cannot find the empty slot, for key %llu\n", key);
			return -1;
		}
		_[slot].value = value;
		_[slot].key = key;
		set_hash(slot, meta_hash, probe);
		return 0;
	}

	void Insert_displace(T key, Value_t value, uint8_t meta_hash, int slot, bool probe){
        _[slot].value = value;
        _[slot].key = key;
#ifdef PMEM
        Allocator::Persist(&_[slot], sizeof(_Pair<T>));
#endif
        mfence();
        set_hash(slot, meta_hash, probe);
	}

	void Insert_displace_with_noflush(T key, Value_t value, uint8_t meta_hash, int slot, bool probe){
		assert(key != 0);
		_[slot].value = value;
		_[slot].key = key;
		set_hash(slot, meta_hash, probe);
	}

	/* Find the displacment element in this bucket*/
	/*
	int Find_displacement(int x){
		for (int i = 0; i < kNumPairPerBucket; ++i)
		{
			auto key_hash = h(&_[i], sizeof(Key_t));
			auto y = BUCKET_INDEX(key_hash);
			if (x == y)
			{
				return i;
			}
		}
		return -1;
	}*/

	inline int Find_org_displacement(){
		int mask = (~(*((int*)membership))) & allocMask;
		if (mask == 0)
		{
			return -1;
		}
		return __builtin_ctz(mask);
	}

	/*find element that it is in the probe*/
	inline int Find_probe_displacement(){
		int mask = (*((int*)membership)) & allocMask;
		if (mask == 0)
		{
			return -1;
		}
		return __builtin_ctz(mask);
	}

	uint32_t version_lock;
	int bitmap; //allocation bitmap + pointer bitmao + counter
	uint8_t finger_array[20];/*only use the first 14 bytes, can be accelerated by SSE instruction,0-13 for finger, 14-17 for overflowed, 18 as the bitmap, 19 as the btimap and overflow check indicator*/
	uint8_t membership[2]; /*Used to test whether the key originally belongs to this bucket*/
	uint8_t overflowMember; /*overflowmember indicates membership of the overflow fingerprint*/ 
	uint8_t overflowCount;
	
	_Pair<T> _[kNumPairPerBucket];
};

template<class T>
struct Table;

template<class T>
struct Directory{
	Table<T> **_;
	uint64_t N_next;
    Directory(){
        Allocator::ZAllocate((void **)&_, kCacheLineSize,
                         sizeof(uint64_t) * segmentSize);
        N_next = baseShifBits << 32;
    }

	Directory(Table<T> **tables){
        _ = tables;
		N_next = baseShifBits << 32;
	}

    static void New(Directory<T> **dir){
        Table<T> **tables{nullptr};
        Allocator::ZAllocate((void **)&tables, kCacheLineSize,
                         sizeof(uint64_t) * segmentSize);
#ifdef PMEM
        auto callback = [](PMEMobjpool *pool, void *ptr, void *arg){
            auto value_ptr = 
                 reinterpret_cast<Table<T> ***>(arg);
            auto dir_ptr = reinterpret_cast<Directory<T> *>(ptr);
            dir_ptr->_ = *value_ptr;
            dir_ptr->N_next = baseShifBits << 32;
            return 0;
        };

        Allocator::Allocate((void **)dir, kCacheLineSize, sizeof(Directory<T>),
                            callback, reinterpret_cast<void*>(&tables));
#else
        Allocator::ZAllocate((void **)dir, kCacheLineSize, sizeof(Directory<T>));
        new (*dir) Directory<T>(tables);   
#endif
    }
};

/* the meta hash-table referenced by the directory*/
template<class T>
struct Table {
  Table(void)
  { 
    //memset((void*)&bucket[0],0,sizeof(struct Bucket)*(kNumBucket+stashBucket)); 
    for (int i = 0; i < kNumBucket; ++i)
    {
    	Bucket<T> *curr_bucket = bucket + i;
    	memset(curr_bucket, 0, 64);
    }

    for (int i = 0; i < stashBucket; ++i)
    {
    	overflowBucket<T> *curr_bucket = stash + i;
    	memset(curr_bucket, 0 , 64);
    }
  }

  static void New(Table<T> **tbl) {
    Allocator::ZAllocate((void **)tbl, kCacheLineSize, sizeof(Table<T>));
  };

  ~Table(void) {}                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     

  int Insert(T key, Value_t value, size_t key_hash, Directory<T> *_dir, uint64_t index, uint32_t old_N, uint32_t old_next);
  void Insert4split(T key, Value_t value, size_t key_hash, uint8_t meta_hash);
  void Split(Table<T> *org_table, Table<T> *expand_table, uint64_t base_level, int org_idx, Directory<T> *);
  int Insert2Org(T key, Value_t value, size_t key_hash, size_t pos);
  void PrintTableImage(Table<T>* table, uint64_t base_level);

  int Next_displace(Bucket<T> *neighbor, Bucket<T>* next_neighbor, T key, Value_t value, uint8_t meta_hash){
	int displace_index = neighbor->Find_org_displacement();
	if ((GET_COUNT(next_neighbor->bitmap) != kNumPairPerBucket) && (displace_index != -1))
	{
		//printf("do the displacement in next bucket, the displaced key is %lld, the new key is %lld\n", neighbor->_[displace_index].key, key);
		next_neighbor->Insert(neighbor->_[displace_index].key, neighbor->_[displace_index].value, neighbor->finger_array[displace_index], true);
		next_neighbor->release_lock();
#ifdef PMEM
		Allocator::Persist(&next_neighbor->bitmap, sizeof(next_neighbor->bitmap));
#endif
		neighbor->unset_hash(displace_index);
		neighbor->Insert_displace(key, value, meta_hash, displace_index, true);
		neighbor->release_lock();
#ifdef PMEM
		Allocator::Persist(&neighbor->bitmap, sizeof(neighbor->bitmap));
#endif
	#ifdef COUNTING
			__sync_fetch_and_add(&number, 1);
	#endif
		return 0;
	}
	return -1;
  }

  int Prev_displace(Bucket<T> *target, Bucket<T>* prev_neighbor, T key, Value_t value, uint8_t meta_hash){
  	int displace_index = target->Find_probe_displacement();
	if ((GET_COUNT(prev_neighbor->bitmap) != kNumPairPerBucket) && (displace_index != -1))
	{
	    //printf("do the displacement in previous bucket,the displaced key is %lld, the new key is %lld\n", target->_[displace_index].key, key);
		prev_neighbor->Insert(target->_[displace_index].key, target->_[displace_index].value, target->finger_array[displace_index], false);
		prev_neighbor->release_lock();
#ifdef PMEM
		Allocator::Persist(&prev_neighbor->bitmap, sizeof(prev_neighbor->bitmap));
#endif
		target->unset_hash(displace_index);
		target->Insert_displace(key, value, meta_hash, displace_index, false);
		target->release_lock();
#ifdef PMEM
		Allocator::Persist(&target->bitmap, sizeof(target->bitmap));
#endif
	#ifdef COUNTING
			__sync_fetch_and_add(&number, 1);
	#endif
		return 0;
	}
	return -1;
  }

  /*insertion in the corresponding position of the stash*/
  int Stash_insert(Bucket<T>* target, Bucket<T>* neighbor, T key, Value_t value, uint8_t meta_hash, int stash_pos){
  	for (int i = 0; i < stashBucket; ++i)
	{
		overflowBucket<T> *curr_bucket = stash + ((stash_pos + i) & stashMask);
		//printf("the stash position is %d\n", (stash_pos + i) & stashMask);
		if (GET_COUNT(curr_bucket->bitmap) < kNumPairPerBucket)
		{
			curr_bucket->Insert(key, value, meta_hash);
#ifdef PMEM
		    Allocator::Persist(&curr_bucket->bitmap, sizeof(curr_bucket->bitmap));
#endif
			target->set_indicator(meta_hash, neighbor, (stash_pos + i) & stashMask);
	#ifdef COUNTING
				__sync_fetch_and_add(&number, 1);
	#endif
			return 0;
		}
	}

	/*need to add the handling the overflowed chaining*/
	overflowBucket<T> *prev_bucket = stash;
	overflowBucket<T> *next_bucket = stash->next;
	while(next_bucket != NULL){
		if (GET_COUNT(next_bucket->bitmap) < kNumPairPerBucket)
		{
			next_bucket->Insert(key, value, meta_hash);
#ifdef PMEM
			Allocator::Persist(&next_bucket->bitmap, sizeof(next_bucket->bitmap));
#endif
			target->set_indicator(meta_hash, neighbor, 3);
	#ifdef COUNTING
				__sync_fetch_and_add(&number, 1);
	#endif
			return 0;
		}
		prev_bucket = next_bucket;
		next_bucket = next_bucket->next;
	}

	/*allocate new next bucket*/
    Allocator::ZAllocate((void **)&prev_bucket->next, kCacheLineSize,
                            sizeof(overflowBucket<T>));
	//prev_bucket->next = new overflowBucket();
#ifdef PMEM
    Allocator::Persist(&prev_bucket->next, sizeof(prev_bucket->next));
#endif
	prev_bucket->next->Insert(key, value, meta_hash);
#ifdef PMEM
	Allocator::Persist(&prev_bucket->next->bitmap, sizeof(prev_bucket->next->bitmap));
#endif
	target->set_indicator(meta_hash, neighbor, 3);
	#ifdef COUNTING
		__sync_fetch_and_add(&number, 1);
	#endif
	return -1;
  }

  inline int verify_access(Directory<T> *new_dir, uint32_t index, uint32_t old_N, uint32_t old_next){
	uint64_t new_N_next = new_dir->N_next;
	uint32_t N = new_N_next >> 32;
	uint32_t next = (uint32_t)new_N_next;

	if (((old_next <= index) && (next > index)) || (old_N != N))
	{
		return -1;
	}
	return 0;
  }

  inline Table<T>* get_org_table(uint64_t x, uint64_t* idx, uint64_t* base_diff, Directory<T> *dir){
	uint64_t base_level = static_cast<uint64_t>(log2(x));
	uint64_t diff = static_cast<uint64_t>(pow(2, base_level));
	*base_diff = diff;
	auto org_idx = x - diff;
	*idx = org_idx;
	uint32_t dir_idx;
	uint32_t offset;
	SEG_IDX_OFFSET(static_cast<uint32_t>(org_idx), dir_idx, offset);
	//printf("the x = %d, the dir_idx = %d, the offset = %d\n", x, dir_idx, offset);
	Table<T> *org_table = dir->_[dir_idx] + offset;
	return org_table;
  }

  Bucket<T> bucket[kNumBucket];
  overflowBucket<T> stash[stashBucket];
};

template<class T>
int Table<T>::Insert2Org(T key, Value_t value, size_t key_hash, size_t pos){
	Bucket<T>* target_bucket = bucket + pos;
	Bucket<T>* neighbor_bucket = bucket + ((pos + 1) & bucketMask);
	uint8_t meta_hash = META_HASH(key_hash);

	int target_num = GET_COUNT(target_bucket->bitmap);
	int neighbor_num = GET_COUNT(neighbor_bucket->bitmap);

	if ((target_num == kNumPairPerBucket) && (neighbor_num == kNumPairPerBucket))
	{
		//printf("insertion in the stash %lld\n", IDX(key_hash, global_N));
		for (int i = 0; i < stashBucket; ++i)
		{
			overflowBucket<T>* curr_bucket = stash + ((i + (pos & stashMask)) & stashMask);
			if (GET_COUNT(curr_bucket->bitmap) < kNumPairPerBucket)
			{
				curr_bucket->Insert(key, value, meta_hash);
#ifdef PMEM
				Allocator::Persist(&curr_bucket->bitmap, sizeof(curr_bucket->bitmap));
#endif
				target_bucket->set_indicator(meta_hash, neighbor_bucket, ((i + (pos & stashMask)) & stashMask));
				return 0;
			}
		}
		return -1;
	}

	if (target_num <= neighbor_num)
	{
		target_bucket->Insert(key, value, meta_hash, false);
#ifdef PMEM
		Allocator::Persist(&target_bucket->bitmap, sizeof(target_bucket->bitmap));
#endif
	}else{
		neighbor_bucket->Insert(key, value, meta_hash, true);
#ifdef PMEM
		Allocator::Persist(&neighbor_bucket->bitmap, sizeof(neighbor_bucket->bitmap));
#endif
	}

	return 0;
}

/*the base_level is used to judge the rehashed key_value should be rehashed to which bucket, the org_idx the index of the original table in the hash index*/
template<class T>
void Table<T>::Split(Table<T> *org_table, Table<T> *expand_table, uint64_t base_level, int org_idx, Directory<T> *_dir){
	Bucket<T> *curr_bucket;
	for (int i = 0; i < kNumBucket; ++i)
	{
		curr_bucket = org_table->bucket + i;
		curr_bucket->get_lock();
	}

	if (!org_table->bucket->test_initialize())
	{
		printf("recursive initiliazation\n");
		uint64_t new_org_idx;
		uint64_t new_base_level;
		Table<T>* new_org_table = get_org_table(org_idx, &new_org_idx, &new_base_level, _dir);
		Split(new_org_table, org_table, new_base_level,new_org_idx, _dir);
	}
	//printf("do the initialization for %d\n", org_idx);

	//printf("expand from bucket %lld to bucekt %lld\n", org_idx, org_idx + base_level);
	/*rehashing of kv objects*/
	size_t key_hash;
	for (int i = 0; i < kNumBucket; ++i)
	{
		curr_bucket = org_table->bucket + i;
		auto mask = GET_BITMAP(curr_bucket->bitmap);
		for (int j = 0; j < kNumPairPerBucket; ++j)
		{
			if (CHECK_BIT(mask, j))
			{
				if constexpr (std::is_pointer_v<T>){
          			key_hash = h(curr_bucket->_[j].key, strlen(curr_bucket->_[j].key));
        		}else{
          			key_hash = h(&(curr_bucket->_[j].key), sizeof(Key_t));
        		}
				//auto x = IDX(key_hash, 2*base_level);
				auto x = key_hash%(2*base_level);
				if (x >= base_level)
				{
					//printf("original: the newly inserted key is %lld\n", curr_bucket->_[j].key);
					expand_table->Insert4split(curr_bucket->_[j].key, curr_bucket->_[j].value, key_hash, curr_bucket->finger_array[j]);
					curr_bucket->unset_hash(j);
	#ifdef COUNTING
					org_table->number--;
	#endif				
				}
			}
		}
	}

	for (int i = 0; i < stashBucket; ++i)
	{
		overflowBucket<T>* curr_bucket = org_table->stash + i;
		auto mask = GET_BITMAP(curr_bucket->bitmap);
		for (int j = 0; j < kNumPairPerBucket; ++j)
		{
			if (CHECK_BIT(mask, j))
			{
				if constexpr (std::is_pointer_v<T>){
          			key_hash = h(curr_bucket->_[j].key, strlen(curr_bucket->_[j].key));
        		}else{
          			key_hash = h(&(curr_bucket->_[j].key), sizeof(Key_t));
        		}

				//auto x = IDX(key_hash, 2*base_level);
				auto x = key_hash%(2*base_level);
				if (x >= base_level)
				{
					//printf("stash: the newly inserted key is %lld\n", curr_bucket->_[j].key);
					expand_table->Insert4split(curr_bucket->_[j].key, curr_bucket->_[j].value, key_hash, curr_bucket->finger_array[j]);
					auto bucket_ix = BUCKET_INDEX(key_hash);
					auto org_bucket = org_table->bucket + bucket_ix;
					auto neighbor_bucket = org_table->bucket + ((bucket_ix + 1) & bucketMask);
					//printf("In stash %d, for slot %d, the cleared key is %lld, the meta_hash is %d\n", i,j,curr_bucket->_[j].key, META_HASH(key_hash));
					org_bucket->unset_indicator(curr_bucket->finger_array[j], neighbor_bucket, curr_bucket->_[j].key, i);
					curr_bucket->unset_hash(j);
	#ifdef COUNTING
					org_table->number--;
	#endif				
				}
			}
		}
	}

	/*traverse the overflow list, I also need to do the rehahsing to the original bucket*/
	overflowBucket<T> *prev_bucket = org_table->stash;
	overflowBucket<T> *next_bucket = org_table->stash->next;
	while(next_bucket != NULL){
		auto mask = GET_BITMAP(next_bucket->bitmap);
		for (int i = 0; i < kNumPairPerBucket; ++i)
		{
			if (CHECK_BIT(mask, i))
			{
				if constexpr (std::is_pointer_v<T>){
          			key_hash = h(next_bucket->_[i].key, strlen(next_bucket->_[i].key));
        		}else{
          			key_hash = h(&(next_bucket->_[i].key), sizeof(Key_t));
        		}

				//auto x = IDX(key_hash, 2*base_level);
				auto x = key_hash%(2*base_level);
				if (x >= base_level)
				{
					expand_table->Insert4split(next_bucket->_[i].key, next_bucket->_[i].value, key_hash, next_bucket->finger_array[i]);
					auto bucket_ix = BUCKET_INDEX(key_hash);
					auto org_bucket = org_table->bucket + bucket_ix;
					auto neighbor_bucket = org_table->bucket + ((bucket_ix + 1) & bucketMask);
					//org_bucket->des_overflow();	
					//printf("overflow: the cleared key is %lld, the meta_hash is %d\n", next_bucket->_[i].key, META_HASH(key_hash));
					org_bucket->unset_indicator(next_bucket->finger_array[i], neighbor_bucket, next_bucket->_[i].key, 3);
					next_bucket->unset_hash(i);
					//if (org_idx == 100)
					//{
					//	PrintTableImage(org_table, base_level);
					//}
	#ifdef COUNTING
					org_table->number--;
	#endif
				}else{
					/*rehashing to original bucket*/
					auto bucket_ix = BUCKET_INDEX(key_hash);
					auto ret = org_table->Insert2Org(next_bucket->_[i].key, next_bucket->_[i].value, key_hash, bucket_ix); /*the unique check may avoid this restore*/
					if (ret == 0)
					{
						auto org_bucket = org_table->bucket + bucket_ix;
						auto neighbor_bucket = org_table->bucket + ((bucket_ix + 1) & bucketMask);
						//org_bucket->des_overflow();	
						//printf("overflow Original Insert: the cleared key is %lld, the meta_hash is %d\n", next_bucket->_[i].key, META_HASH(key_hash));
						org_bucket->unset_indicator(next_bucket->finger_array[i], neighbor_bucket, next_bucket->_[i].key, 3);
						next_bucket->unset_hash(i);
						/*
						if (org_idx == 100)
						{
							PrintTableImage(org_table, base_level);
						}*/
					}
				}
			}
		}

		if (GET_COUNT(next_bucket->bitmap) == 0)
		{
			//printf("restore the bucket\n");
			prev_bucket->next = next_bucket->next;
			//delete next_bucket;
            Allocator::Free(next_bucket);
			next_bucket = prev_bucket->next;
		}else{
			prev_bucket = next_bucket;
			next_bucket = next_bucket->next;
		}
	}

	/*clear the uintialized bit in expand_table*/
	for (int i = 0; i < kNumBucket; ++i)
	{
		curr_bucket = expand_table->bucket + i;
		curr_bucket->set_initialize();
	}

	//clflush((char*)expand_table, sizeof(Table));
#ifdef PMEM
    Allocator::Persist(expand_table, sizeof(Table));
#endif
	/*flush the overflowed bucket*/
	prev_bucket = expand_table->stash;
	next_bucket = prev_bucket->next;

	while(next_bucket != NULL){
		//clflush((char*)next_bucket, sizeof(struct Bucket));
#ifdef PMEM
        Allocator::Persist(next_bucket, sizeof(struct overflowBucket<T>));
#endif
		prev_bucket = next_bucket;
		next_bucket = next_bucket->next;
	}

	if constexpr (std::is_pointer_v<T>) {
#ifdef PMEM
		Allocator::Persist(org_table, sizeof( Table));

		prev_bucket = org_table->stash;
		next_bucket = prev_bucket->next;

		while(next_bucket != NULL){
			Allocator::Persist(next_bucket, sizeof(struct overflowBucket<T>));
		}
		prev_bucket = next_bucket;
		next_bucket = next_bucket->next;
#endif
	}
	for (int i = 0; i < kNumBucket; ++i)
	{
		curr_bucket = org_table->bucket + i;
		curr_bucket->release_lock();
	}
	//printf("finish initialization\n");
}

/* it needs to verify whether this bucket has been deleted...*/
template<class T>
int Table<T>::Insert(T key, Value_t value, size_t key_hash, Directory<T> *_dir, uint64_t index, uint32_t old_N, uint32_t old_next){
	/*we need to first do the locking and then do the verify*/
	uint8_t meta_hash = META_HASH(key_hash);
	auto y = BUCKET_INDEX(key_hash);
	Bucket<T>* target = bucket + y;
	Bucket<T>* neighbor = bucket + ((y+1) & bucketMask);
	//printf("for key %lld, target bucket is %lld, meta_hash is %d\n", key, BUCKET_INDEX(key_hash), meta_hash);
	target->get_lock();
	if(!neighbor->try_get_lock()){
		target->release_lock();
		return -2;
	}

	if (!target->test_initialize())
	{
		//printf("initialize bucket %d\n", index);
		neighbor->release_lock();
		target->release_lock();
		for (int i = 0; i < kNumBucket; ++i)
		{
			Bucket<T> *curr_bucket = bucket + i;
			curr_bucket->get_lock();
		}
		//printf("I enter the lock region\n");

		auto ret = verify_access(_dir, index, old_N, old_next);
		if (ret == -1)
		{
			for (int i = 0; i < kNumBucket; ++i)
			{
				Bucket<T> *curr_bucket = bucket + i;
				curr_bucket->release_lock();
			}
			return -2;
		}
		//printf("finish the verify process\n");

		uint64_t org_idx;
		uint64_t base_level;
		/*org_idx is the index of the original table, the base_level is the index diff between original table and target table*/
		Table<T>* org_table = get_org_table(index, &org_idx, &base_level, _dir);
		//printf("get the org_table\n");
		/*the split process splits from original table to target table*/
		Split(org_table, this, base_level, org_idx, _dir);

		//printf("finish split process\n");
		for (int i = 0; i < kNumBucket; ++i)
		{
			Bucket<T> *curr_bucket = bucket + i;
			curr_bucket->release_lock();
		}
		return -2; /*return retry to reinsert the key-value*/
	}else{
		auto ret = verify_access(_dir, index, old_N, old_next);
		if (ret == -1)
		{
			neighbor->release_lock();
			target->release_lock();
			return -2;
		}

		/*the unique_check is to check whether the key has existed*/
		ret = target->unique_check(meta_hash, key, neighbor, stash);
		if (ret == -1)
		{
			neighbor->release_lock();
			target->release_lock();
			return 0;
		}

		int target_num = GET_COUNT(target->bitmap);
		int neighbor_num = GET_COUNT(neighbor->bitmap);
		if ((target_num == kNumPairPerBucket) && (neighbor_num == kNumPairPerBucket))
		{
			/* overflow handling */
			Bucket<T> *next_neighbor = bucket + ((y+2) & bucketMask);
			//Next displacement
			if(!next_neighbor->try_get_lock()){
				neighbor->release_lock();
				target->release_lock();
				return -2;
			}
			auto ret = Next_displace(neighbor, next_neighbor, key, value, meta_hash);
			if (ret == 0)
			{
				target->release_lock();
				return 0;
			}		
			next_neighbor->release_lock();

			Bucket<T> *prev_neighbor;
			int prev_index;
			if (y == 0)
			{
				prev_neighbor = bucket + kNumBucket - 1;
				prev_index = kNumBucket - 1;
			}else{
				prev_neighbor = bucket + y - 1;
				prev_index = y - 1;
			}

			if(!prev_neighbor->try_get_lock()){
				target->release_lock();
				neighbor->release_lock();
				return -2;
			}

			ret = Prev_displace(target, prev_neighbor, key, value, meta_hash);
			if (ret == 0)
			{
				neighbor->release_lock();
				return 0;
			}

			stash->get_lock();
			ret = Stash_insert(target, neighbor, key, value, meta_hash, y & stashMask);

			stash->release_lock();
			neighbor->release_lock();
			target->release_lock();
			prev_neighbor->release_lock();
			return ret;
		}

		if (target_num <= neighbor_num)
		{
			target->Insert(key, value, meta_hash, false);
			target->release_lock();
#ifdef PMEM
			Allocator::Persist(&target->bitmap, sizeof(target->bitmap));
#endif
			neighbor->release_lock();
		}else{
			neighbor->Insert(key, value, meta_hash, true);
			neighbor->release_lock();
#ifdef PMEM
			Allocator::Persist(&neighbor->bitmap, sizeof(neighbor->bitmap));
#endif
			target->release_lock();
		}

		#ifdef COUNTING
		__sync_fetch_and_add(&number, 1);
		#endif
		return 0;
	}
}

/*the insert needs to be perfectly balanced, not destory the power of balance*/
template<class T>
void Table<T>::Insert4split(T key, Value_t value, size_t key_hash, uint8_t meta_hash){
	auto y = BUCKET_INDEX(key_hash);
	Bucket<T>* target = bucket + y;
	Bucket<T>* neighbor = bucket + ((y+1) & bucketMask);
	//auto insert_target = (target->count&lowCountMask)<=(neighbor->count&lowCountMask)?target:neighbor;
	Bucket<T>* insert_target;
	bool probe = false;
	if (GET_COUNT(target->bitmap) <= GET_COUNT(neighbor->bitmap))
	{
		insert_target = target;
	}else{
		insert_target = neighbor;
		probe = true;
	}

	//assert(insert_target->count < kNumPairPerBucket);
	/*some bucket may be overflowed?*/
	if (GET_COUNT(insert_target->bitmap) < kNumPairPerBucket)
	{
		insert_target->_[GET_COUNT(insert_target->bitmap)].key = key;
		insert_target->_[GET_COUNT(insert_target->bitmap)].value = value;
		insert_target->set_hash(GET_COUNT(insert_target->bitmap), meta_hash, probe);
	#ifdef COUNTING
			++number;
	#endif
	}else{
		/*do the displacement or insertion in the stash*/
		Bucket<T> *next_neighbor = bucket + ((y+2) & bucketMask);
		int displace_index;
		displace_index = neighbor->Find_org_displacement();
		if (((GET_COUNT(next_neighbor->bitmap)) != kNumPairPerBucket) && (displace_index != -1))
		{
			//printf("do the displacement in next bucket, the displaced key is %lld, the new key is %lld\n", neighbor->_[displace_index].key, key);
			next_neighbor->Insert_with_noflush(neighbor->_[displace_index].key, neighbor->_[displace_index].value, neighbor->finger_array[displace_index], true);
			neighbor->unset_hash(displace_index);
			neighbor->Insert_displace_with_noflush(key, value, meta_hash, displace_index, true);
	#ifdef COUNTING
				++number;
	#endif
			return;
		}
		Bucket<T> *prev_neighbor;
		int prev_index;
		if (y == 0)
		{
			prev_neighbor = bucket + kNumBucket - 1;
			prev_index = kNumBucket - 1;
		}else{
			prev_neighbor = bucket + y - 1;
			prev_index = y - 1;
		}

		displace_index = target->Find_probe_displacement();
		if (((GET_COUNT(prev_neighbor->bitmap)) != kNumPairPerBucket) && (displace_index != -1))
		{
			//printf("do the displacement in previous bucket,the displaced key is %lld, the new key is %lld\n", target->_[displace_index].key, key);
			prev_neighbor->Insert_with_noflush(target->_[displace_index].key, target->_[displace_index].value, target->finger_array[displace_index], false);
			target->unset_hash(displace_index);
			target->Insert_displace_with_noflush(key, value, meta_hash, displace_index, false);
	#ifdef COUNTING
				++number;
	#endif
			return;
		}

		Stash_insert(target, neighbor, key, value, meta_hash, y & stashMask);
	}	
}

template<class T>
class Linear{
	public:
    Linear(void);
    Linear(size_t);
    ~Linear(void);
    int Insert(T key, Value_t value);
    bool Delete(T);
    Value_t Get(T);
    void FindAnyway(T key);
    void GetBasicInfo(){
    	uint64_t count = 0;
		uint64_t prev_length = 0;
		uint64_t after_length = 0;
		uint64_t Bucket_num = 0;
    	for (int idx = 0; idx < partitionNum; ++idx)
    	{
	    	uint64_t old_N_next = dir[idx].N_next;
	    	uint32_t N = old_N_next >> 32;
	    	uint32_t next = (uint32_t)old_N_next;
			uint32_t occupied_bucket = pow2(N) + next;

			for (int i = 0; i < occupied_bucket; ++i)
			{
				uint32_t dir_idx;
				uint32_t offset;
				SEG_IDX_OFFSET(i, dir_idx, offset);
				Table<T> *curr_table = dir[idx]._[dir_idx] + offset;
				for (int j = 0; j < kNumBucket; ++j)
				{
					Bucket<T>* curr_bucket = curr_table->bucket + j;
					count +=  GET_COUNT(curr_bucket->bitmap);
					int mask = GET_BITMAP(curr_bucket->bitmap);
	  				int micro_count = 0;
			  		if (mask != 0)
			  		{
			  			for (int k = 0; k < kNumPairPerBucket; ++k)
			  			{
			  				if (CHECK_BIT(mask, k))
			  				{
			  					micro_count++;
			  					//printf("%lld in %d Bucket, in %d table\n", curr_bucket->_[k].key, j, i);
			  				}
						}
					}
					assert(micro_count == GET_COUNT(curr_bucket->bitmap));
				}

				for (int j = 0; j < stashBucket; ++j)
				{	
					overflowBucket<T>* curr_bucket = curr_table->stash + j;
					count +=  GET_COUNT(curr_bucket->bitmap);
					int mask = GET_BITMAP(curr_bucket->bitmap);
			  		int micro_count = 0;
			  		if (mask != 0)
			  		{
			  			for (int k = 0; k < kNumPairPerBucket; ++k)
			  			{
			  				if (CHECK_BIT(mask, k))
			  				{
			  					micro_count++;
			  					//printf("%lld in %d stash Bucket, in %d table\n", curr_bucket->_[k].key, j, i);
			  				}
						}
					}
					assert(micro_count == GET_COUNT(curr_bucket->bitmap));
				}

				overflowBucket<T> *prev_bucket = curr_table->stash;
				overflowBucket<T> *next_bucket = prev_bucket->next;
				while(next_bucket != NULL){
					count += GET_COUNT(next_bucket->bitmap);
					int mask = GET_BITMAP(next_bucket->bitmap);
			  		int micro_count = 0;
			  		if (mask != 0)
			  		{
			  			for (int k = 0; k < kNumPairPerBucket; ++k)
			  			{
			  				if (CHECK_BIT(mask, k))
			  				{
			  					micro_count++;
			  					//printf("%lld in overflow Bucket, in %d table\n", next_bucket->_[k].key, i);
			  				}
						}
					}
					assert(micro_count == GET_COUNT(next_bucket->bitmap));
					prev_bucket = next_bucket;
					next_bucket = next_bucket->next;
					if (i < next)
					{
						prev_length++;
					}else{
						after_length++;
					}
					Bucket_num++;
				}
			}
			Bucket_num += SUM_BUCKET(occupied_bucket-1)*(kNumBucket + stashBucket);
		}
		//printf("the N = %lld, the next = %lld\n", N, next);
		//uint64_t seg_num = (N + next - 1)/segmentSize + 1;
		
		//printf("the size of Table is %lld\n", sizeof(struct Table));
		//printf("the size of Bucekt is %lld\n", sizeof(struct Bucket));
		//printf("the size of oveflow bucket is %lld\n", sizeof(struct overflowBucket));
		printf("the inserted num is %lld\n", count);
		//printf("the segment number is %lld\n", seg_num);
		printf("the bucket number is %lld\n", Bucket_num);
		printf("the local load factor = %lf\n", (double)count/(Bucket_num*kNumPairPerBucket));
		printf("the local raw sapce utilization = %lf\n", (double)count/(Bucket_num*16));
		printf("the prev_length = %lld\n", prev_length);
		printf("the after_length = %lld\n", after_length);
//		printf("the prev_average length = %lf\n", (double)prev_length/next);
//		printf("the after_average length = %lf\n", (double)after_length/N);
//		printf("the average length = %lf\n", (double)(prev_length+after_length)/(next+N));
//    	printf("the overflow access is %lu\n", overflow_access);
    }

    inline void Expand(uint64_t partiton_idx, uint32_t numBuckets){
   	RE_EXPAND:
    	uint64_t old_N_next = dir[partiton_idx].N_next;
    	uint32_t old_N = old_N_next >> 32;
    	uint32_t old_next = (uint32_t)old_N_next;
    	uint32_t dir_idx;
		uint32_t offset;
		SEG_IDX_OFFSET(static_cast<uint32_t>(pow(2, old_N)) + old_next + numBuckets - 1, dir_idx, offset);
		//printf("for bucket index id %d, the segment index is %d, the internal offset is %d\n", static_cast<uint32_t>(pow(2, old_N)) + old_next + numBuckets - 1, dir_idx, offset);
    	/*first need the reservation of the key-value*/
    	Table<T>* RESERVED = reinterpret_cast<Table<T> *>(-1);
    	if (dir[partiton_idx]._[dir_idx] == RESERVED) 
    	{
    		goto RE_EXPAND;
    		//return;
    	}

    	/*first need to reserve the position for the memory allocation*/
    	Table<T>* old_value = NULL;
    	if (dir[partiton_idx]._[dir_idx] == NULL)
    	{
    		if (CAS(&(dir[partiton_idx]._[dir_idx]), &old_value, RESERVED))
    		{
    			#ifdef DOUBLE_EXPANSION
    			uint32_t seg_size = SEG_SIZE(static_cast<uint32_t>(pow(2, old_N)) + old_next + numBuckets - 1);
    			//dir[partiton_idx]._[dir_idx] = new Table[seg_size];
                Allocator::ZAllocate((void**)&dir[partiton_idx]._[dir_idx], kCacheLineSize,
                                    sizeof(Table<T>) * seg_size);
    			//printf("the new table size for partition %d is %d\n", partiton_idx,seg_size);
    			//printf("expansion to %u on %u\n",seg_size, dir_idx);
    			//dir->_[dir_idx] = new Table[segmentSize*pow2(dir_idx-1)];
    			//dir->_[dir_idx] = new Table[segmentSize*pow2(dir_idx)];
    			#else
                Allocator::ZAllocate((void**)&dir[partiton_idx]._[dir_idx], kCacheLineSize,
                                    sizeof(Table) * segmentSize);
    			#endif
#ifdef PMEM
                Allocator::Persist(&dir[partiton_idx]._[dir_idx], sizeof(Table<T> *));
#endif
    		}else{
    			goto RE_EXPAND;
    		}
    	}

    	uint64_t new_N_next;
    	//if (old_next == (segmentSize*static_cast<uint64_t>(pow(2, old_N)) - numBuckets))
    	if (old_next == (static_cast<uint64_t>(pow(2, old_N)) - numBuckets))
    	{
    		new_N_next = (((uint64_t)(old_N + 1)) << 32);
    	}else{
    		new_N_next = (old_N_next & high32Mask) + (old_next + numBuckets);
    	}

    	if(!CAS(&dir[partiton_idx].N_next, &old_N_next, new_N_next)){
    		goto RE_EXPAND;
    	}

#ifdef PMEM
        Allocator::Persist(&dir[partiton_idx].N_next, sizeof(uint64_t));
#endif
    	/*
    	if ((uint32_t)new_N_next == 0)
    	{
    		printf("expand to level %lu\n", new_N_next >> 32);
    	}*/

    	//printf("Expand from %d,%d to %d,%d\n", old_N, old_next, new_N_next >> 32, (uint32_t)new_N_next);
    }

    Directory<T> dir[partitionNum];
#ifdef PMEM
    PMEMobjpool *pool_addr;
#endif
};

template<class T>
Linear<T>::Linear(void){
	for (int i = 0; i < partitionNum; ++i)
	{
		dir[i].N_next = baseShifBits << 32;
		//dir[i]._[0] = new Table[segmentSize];
        Allocator::ZAllocate((void **)&dir[i]._[0], kCacheLineSize, sizeof(Table<T>)*segmentSize);
		Table<T>* curr_table = dir[i]._[0];
		for (int j = 0; j < segmentSize; ++j)
		{
			curr_table = dir[i]._[0] + j;
			for (int k = 0; k < kNumBucket; ++k)
			{
				Bucket<T> *curr_bucket = curr_table->bucket + k;
				curr_bucket->set_initialize();
			}
		}
	}
}

template<class T>
Linear<T>::~Linear(void){
	//TO-DO
}

template<class T>
int Linear<T>::Insert(T key, Value_t value) {
    uint64_t key_hash;
	if constexpr (std::is_pointer_v<T>){
		key_hash = h(key, strlen(key));
	}else{
		key_hash = h(&key, sizeof(key));
	}
   auto partiton_idx = PARTITION_INDEX(key_hash);
RETRY:
	uint64_t old_N_next = dir[partiton_idx].N_next;
	uint32_t N = old_N_next >> 32;
	uint32_t next = (uint32_t)old_N_next;
	//printf("insertion for key %lu\n", key);

	auto x = IDX(key_hash, N);
	if (x < next)
	{
		x = IDX(key_hash, N+1);
	}

	uint32_t dir_idx;
	uint32_t offset;
	SEG_IDX_OFFSET(static_cast<uint32_t>(x), dir_idx, offset);
	//printf("the dir_idx is %d, the offset is %d\n", dir_idx, offset);

	//assert(dir_idx < directorySize);
	Table<T>* target = dir[partiton_idx]._[dir_idx] + offset;
	Table<T>* RESERVED = reinterpret_cast<Table<T> *>(-1);
	//assert(dir[partiton_idx]._[dir_idx] != RESERVED);
	//assert(dir[partiton_idx]._[dir_idx] != NULL);

	//printf("insert for key %lld, the bucket_idx is %d, the dir_idx is %d, the offset is %d\n", key, x,dir_idx, offset);
	auto ret = target->Insert(key, value, key_hash, &(dir[partiton_idx]), x, N, next);

	if (ret == -2)
	{
		goto RETRY;
	}else if(ret == -1){
		Expand(partiton_idx, 1);
	}
  	return 0;
}

template<class T>
Value_t Linear<T>::Get(T key){
   uint64_t key_hash;
	if constexpr (std::is_pointer_v<T>){
		key_hash = h(key, strlen(key));
	}else{
		key_hash = h(&key, sizeof(key));
	}
   auto partiton_idx = PARTITION_INDEX(key_hash);
   auto meta_hash = META_HASH(key_hash);   
   auto y = BUCKET_INDEX(key_hash);
RETRY:

	uint64_t old_N_next = dir[partiton_idx].N_next;
	uint32_t N = old_N_next >> 32;
	uint32_t next = (uint32_t)old_N_next;

	auto x = IDX(key_hash, N);
	if (x < next)
	{
		x = IDX(key_hash, N+1);
	}

	uint32_t dir_idx;
	uint32_t offset;
	SEG_IDX_OFFSET(static_cast<uint32_t>(x), dir_idx, offset);
	Table<T>* target = dir[partiton_idx]._[dir_idx] + offset;
  
	uint32_t old_version;
	uint32_t old_neighbor_version;
	Bucket<T> *target_bucket = target->bucket + y;
	Bucket<T> *neighbor_bucket = target->bucket + ((y+1) & bucketMask);
	//printf("Get key %lld, x = %d, y = %d, meta_hash = %d\n", key, x, BUCKET_INDEX(key_hash), meta_hash);

	if (target_bucket->test_lock_set(old_version) || neighbor_bucket->test_lock_set(old_neighbor_version))
	{
		goto RETRY;
	}

	if (!target_bucket->test_initialize())
	{
		for (int i = 0; i < kNumBucket; ++i)
		{
			Bucket<T> *curr_bucket = target->bucket + i;
			curr_bucket->get_lock();
		}

		uint64_t new_N_next = dir[partiton_idx].N_next;
		uint32_t new_N = new_N_next >> 32;
		uint32_t new_next = (uint32_t)new_N_next;
		if (((next <= x) && (new_next > x)) || (new_N != N))
		{
			for (int i = 0; i < kNumBucket; ++i)
			{
				Bucket<T> *curr_bucket = target->bucket + i;
				curr_bucket->release_lock();
			}
			goto RETRY;
		}

		uint64_t org_idx;
		uint64_t base_level;
		Table<T>* org_table = target->get_org_table(x, &org_idx, &base_level, &(dir[partiton_idx]));
		target->Split(org_table, target, base_level, org_idx, &(dir[partiton_idx]));

		for (int i = 0; i < kNumBucket; ++i)
		{
			Bucket<T> *curr_bucket = target->bucket + i;
			curr_bucket->release_lock();
		}
		goto RETRY;
	}else{
		uint64_t new_N_next = dir[partiton_idx].N_next;
		uint32_t new_N = new_N_next >> 32;
		uint32_t new_next = (uint32_t)new_N_next;
		if (((next <= x) && (new_next > x)) || (new_N != N))
		{
			goto RETRY;
		}

		auto ret = target_bucket->check_and_get(meta_hash, key, false);
		if ((ret != NONE) && (!(target_bucket->test_lock_version_change(old_version))))
		{
			return ret;
		}
	  	
		/*no need for verification procedure, we use the version number of target_bucket to test whether the bucket has ben spliteted*/
		ret = neighbor_bucket->check_and_get(meta_hash, key, true);
		if ((ret != NONE) && (!(target_bucket->test_lock_version_change(old_version))))
		{
			return ret;
		}

		//return _INVALID;
		if(target_bucket->test_stash_check())
		{
			auto test_stash = false;
			if (target_bucket->test_overflow())
			{	
				//this only occur when the bucket has more key-values than 10 that are overfloed int he shared bucket area, therefore it needs to search in the extra bucket
				test_stash = true;
			}else{
				//search in the original bucket
				int mask = target_bucket->finger_array[14];
				if (mask != 0)
				{
					for (int i = 0; i < 4; ++i)
					{
						if (CHECK_BIT(mask, i) && (target_bucket->finger_array[15+i] == meta_hash) && (((1 << i) & target_bucket->overflowMember) == 0))
						{
							test_stash = true;
							goto TEST_STASH;
						}
					}
				}

				mask = neighbor_bucket->finger_array[14];
				if (mask != 0)
				{
					for (int i = 0; i < 4; ++i)
					{
						if (CHECK_BIT(mask, i) && (neighbor_bucket->finger_array[15+i] == meta_hash) && (((1 << i) & neighbor_bucket->overflowMember) != 0))
						{
							test_stash = true;
							break;
						}
					}	
				}
			}
		TEST_STASH:
			if (test_stash == true)
			{
				//overflow_access++;
				for (int i = 0; i < stashBucket; ++i)
				{
					overflowBucket<T> *curr_bucket = target->stash + ((i + (y & stashMask)) & stashMask);
					auto ret = curr_bucket->check_and_get(meta_hash, key);
					if (ret != NONE)
					{
						if (target_bucket->test_lock_version_change(old_version))
						{
						  goto RETRY;
						}
						return ret;
					}
				}

				overflowBucket<T> *prev_bucket = target->stash;
				overflowBucket<T> *next_bucket = target->stash->next;
				while(next_bucket != NULL){
					auto ret = next_bucket->check_and_get(meta_hash, key);
					if (ret != NONE)
					{
						if (target_bucket->test_lock_version_change(old_version))
						{
						  goto RETRY;
						}
						return ret;
					}
					prev_bucket = next_bucket;
					next_bucket = next_bucket->next;
				}
			}
		}
	}
	//printf("the x = %lld, the y = %lld, the meta_hash is %d\n", x, y, meta_hash);
	return NONE;
}

/*the delete operation of the */
template<class T>
 bool Linear<T>::Delete(T key){
   uint64_t key_hash;
	if constexpr (std::is_pointer_v<T>){
		key_hash = h(key, strlen(key));
	}else{
		key_hash = h(&key, sizeof(key));
	}
   auto partiton_idx = PARTITION_INDEX(key_hash);
   auto meta_hash = META_HASH(key_hash);   
   auto y = BUCKET_INDEX(key_hash);
RETRY:
	uint64_t old_N_next = dir[partiton_idx].N_next;
	uint32_t N = old_N_next >> 32;
	uint32_t next = (uint32_t)old_N_next;

	auto x = IDX(key_hash, N);
	if (x < next)
	{
		x = IDX(key_hash, N+1);
	}

	uint32_t dir_idx;
	uint32_t offset;
	SEG_IDX_OFFSET(static_cast<uint32_t>(x), dir_idx, offset);
	Table<T>* target = dir[partiton_idx]._[dir_idx] + offset;
  
	uint32_t old_version;
	Bucket<T> *target_bucket = target->bucket + y;
	Bucket<T> *neighbor_bucket = target->bucket + ((y+1) & bucketMask);
	//printf("Get key %lld, x = %d, y = %d, meta_hash = %d\n", key, x, BUCKET_INDEX(key_hash), meta_hash);

	target_bucket->get_lock();
	if(!neighbor_bucket->try_get_lock()){
		target_bucket->release_lock();
		goto RETRY;
	}

	if (!target_bucket->test_initialize())
	{
        target_bucket->release_lock();
		neighbor_bucket->release_lock();
		for (int i = 0; i < kNumBucket; ++i)
		{
			Bucket<T> *curr_bucket = target->bucket + i;
			curr_bucket->get_lock();
		}

		uint64_t new_N_next = dir[partiton_idx].N_next;
		uint32_t new_N = new_N_next >> 32;
		uint32_t new_next = (uint32_t)new_N_next;
		if (((next <= x) && (new_next > x)) || (new_N != N))
		{
			for (int i = 0; i < kNumBucket; ++i)
			{
				Bucket<T> *curr_bucket = target->bucket + i;
				curr_bucket->release_lock();
			}
			goto RETRY;
		}

		uint64_t org_idx;
		uint64_t base_level;
		Table<T>* org_table = target->get_org_table(x, &org_idx, &base_level, &(dir[partiton_idx]));
		target->Split(org_table, target, base_level, org_idx, &(dir[partiton_idx]));

		for (int i = 0; i < kNumBucket; ++i)
		{
			Bucket<T> *curr_bucket = target->bucket + i;
			curr_bucket->release_lock();
		}
		goto RETRY;
	}else{
		uint64_t new_N_next = dir[partiton_idx].N_next;
		uint32_t new_N = new_N_next >> 32;
		uint32_t new_next = (uint32_t)new_N_next;
		if (((next <= x) && (new_next > x)) || (new_N != N))
		{
            target_bucket->release_lock();
			neighbor_bucket->release_lock();
			goto RETRY;
		}

		auto ret = target_bucket->Delete(meta_hash, key, false);
        if(ret == 0){
			target_bucket->release_lock();
#ifdef PMEM
			Allocator::Persist(&target_bucket->bitmap, sizeof(target_bucket->bitmap));		
#endif
			neighbor_bucket->release_lock();
            return true;
        }
    
		/*no need for verification procedure, we use the version number of target_bucket to test whether the bucket has ben spliteted*/
		ret = neighbor_bucket->Delete(meta_hash, key, true);
        if(ret == 0){
			neighbor_bucket->release_lock();
#ifdef PMEM
			Allocator::Persist(&neighbor_bucket->bitmap, sizeof(neighbor_bucket->bitmap));
#endif
            target_bucket->release_lock();
            return true;
        }

		if(target_bucket->test_stash_check())
		{
			auto test_stash = false;
			if (target_bucket->test_overflow())
			{	
				//this only occur when the bucket has more key-values than 10 that are overfloed int he shared bucket area, therefore it needs to search in the extra bucket
				test_stash = true;
			}else{
				//search in the original bucket
				int mask = target_bucket->finger_array[14];
				if (mask != 0)
				{
					for (int i = 0; i < 4; ++i)
					{
						if (CHECK_BIT(mask, i) && (target_bucket->finger_array[15+i] == meta_hash) && (((1 << i) & target_bucket->overflowMember) == 0))
						{
							test_stash = true;
							goto TEST_STASH;
						}
					}
				}

				mask = neighbor_bucket->finger_array[14];
				if (mask != 0)
				{
					for (int i = 0; i < 4; ++i)
					{
						if (CHECK_BIT(mask, i) && (neighbor_bucket->finger_array[15+i] == meta_hash) && (((1 << i) & neighbor_bucket->overflowMember) != 0))
						{
							test_stash = true;
							break;
						}
					}	
				}
			}
		TEST_STASH:
			if (test_stash == true)
			{
				//overflow_access++;
				overflowBucket<T> *stash = target->stash;
				stash->get_lock();
				for (int i = 0; i < stashBucket; ++i)
				{
					int index = ((i + (y & stashMask)) & stashMask);
					overflowBucket<T> *curr_bucket = target->stash + index;
					auto ret = curr_bucket->Delete(meta_hash, key);
					if (ret == 0)
					{
						stash->release_lock();
#ifdef PMEM
						Allocator::Persist(&curr_bucket->bitmap, sizeof(curr_bucket->bitmap));
#endif
                        target_bucket->unset_indicator(meta_hash, neighbor_bucket, key, index);
                        target_bucket->release_lock();
                        neighbor_bucket->release_lock();
						return true;
					}
				}
				
				overflowBucket<T> *prev_bucket = target->stash;
				overflowBucket<T> *next_bucket = target->stash->next;
				while(next_bucket != NULL){
					auto ret = next_bucket->Delete(meta_hash, key);
					if (ret == 0)
					{
						stash->release_lock();
#ifdef PMEM
						Allocator::Persist(&next_bucket->bitmap, sizeof(next_bucket->bitmap));
#endif
                        target_bucket->unset_indicator(meta_hash, neighbor_bucket, key, 3);
						target_bucket->release_lock();
                        neighbor_bucket->release_lock();
						return true;
					}
					prev_bucket = next_bucket;
					next_bucket = next_bucket->next;
				}
				stash->release_lock();
			}
		}
	}
    target_bucket->release_lock();
    neighbor_bucket->release_lock();
	//printf("the x = %lld, the y = %lld, the meta_hash is %d\n", x, y, meta_hash);
	return false;
 }
#endif