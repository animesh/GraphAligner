#ifndef WordSlice_h
#define WordSlice_h

#include "ByteStuff.h"

template <typename Word>
class WordConfiguration
{
};

template <>
class WordConfiguration<uint64_t>
{
public:
	static constexpr int WordSize = 64;
	//number of bits per chunk
	//prefix sum differences are calculated in chunks of log w bits
	static constexpr int ChunkBits = 8;
	static constexpr uint64_t AllZeros = 0x0000000000000000;
	static constexpr uint64_t AllOnes = 0xFFFFFFFFFFFFFFFF;
	static constexpr uint64_t LastBit = 0x8000000000000000;
	//positions of the sign bits for each chunk
	static constexpr uint64_t SignMask = 0x8080808080808080;
	//constant for multiplying the chunk popcounts into prefix sums
	//this should be 1 at the start of each chunk
	static constexpr uint64_t PrefixSumMultiplierConstant = 0x0101010101010101;
	//positions of the least significant bits for each chunk
	static constexpr uint64_t LSBMask = 0x0101010101010101;

#ifdef NOBUILTINPOPCOUNT
	static int popcount(uint64_t x)
	{
		//https://en.wikipedia.org/wiki/Hamming_weight
		x -= (x >> 1) & 0x5555555555555555;
		x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
		x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f;
		return (x * 0x0101010101010101) >> 56;
	}
#else
	static int popcount(uint64_t x)
	{
		//https://gcc.gnu.org/onlinedocs/gcc-4.8.4/gcc/X86-Built-in-Functions.html
		// return __builtin_popcountll(x);
		//for some reason __builtin_popcount takes 21 instructions so call assembly directly
		__asm__("popcnt %0, %0" : "+r" (x));
		return x;
	}
#endif


	static uint64_t ChunkPopcounts(uint64_t value)
	{
		uint64_t x = value;
		x -= (x >> 1) & 0x5555555555555555;
		x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
		x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f;
		return x;
	}

	static int BitPosition(uint64_t low, uint64_t high, int rank)
	{
		assert(rank >= 0);
		if (popcount(low) <= rank)
		{
			rank -= popcount(low);
			return 64 + BitPosition(high, rank);
		}
		return BitPosition(low, rank);
	}

	static int BitPosition(uint64_t number, int rank)
	{
		// // https://stackoverflow.com/questions/45482787/how-to-efficiently-find-the-n-th-set-bit
		// uint64_t j = _pdep_u64(1 << rank, number);
		// return (__builtin_ctz(j));
		uint64_t bytes = ChunkPopcounts(number);
		//cumulative popcount of each byte
		uint64_t cumulative = bytes * PrefixSumMultiplierConstant;
		//spread the rank into each byte
		uint64_t rankFinder = ((rank + 1) & 0xFF) * PrefixSumMultiplierConstant;
		//rankMask's msb will be 0 if the c. popcount at that byte is < rank, or 1 if >= rank
		uint64_t rankMask = (cumulative | SignMask) - rankFinder;
		//the total number of ones in rankMask is the number of bytes whose c. popcount is >= rank
		//8 - that is the number of bytes whose c. popcount is < rank
		int smallerBytes = 8 - ((((rankMask & SignMask) >> 7) * PrefixSumMultiplierConstant) >> 56);
		assert(smallerBytes < 8);
		//the bit position will be inside this byte
		uint64_t interestingByte = (number >> (smallerBytes * 8)) & 0xFF;
		if (smallerBytes > 0) rank -= (cumulative >> ((smallerBytes - 1) * 8)) & 0xFF;
		assert(rank >= 0 && rank < 8);
		//spread the 1's from interesting byte to each byte
		//first put every pair of bits into each 2-byte boundary
		//then select only those pairs
		//then spread the pairs into each byte boundary
		//and select the ones
		uint64_t spreadBits = (((interestingByte * 0x0000040010004001) & 0x0003000300030003) * 0x0000000000000081) & 0x0101010101010101;
/*
0000 0000  0000 0000  0000 0000  0000 0000  0000 0000  0000 0000  0000 0000  abcd efgh
0000 0000  0000 00ab  cdef gh00  0000 abcd  efgh 0000  00ab cdef  gh00 0000  abcd efgh  * 0x0000040010004001
0000 0000  0000 00ab  0000 0000  0000 00cd  0000 0000  0000 00ef  0000 0000  0000 00gh  & 0x0003000300030003
0000 000a  b000 00ab  0000 000c  d000 00cd  0000 000e  f000 00ef  0000 000g  h000 00gh  * 0x0000000000000081
0000 000a  0000 000b  0000 000c  0000 000d  0000 000e  0000 000f  0000 000g  0000 000h  & 0x0101010101010101
*/
		//find the position from the bits the same way as from the bytes
		uint64_t cumulativeBits = spreadBits * PrefixSumMultiplierConstant;
		uint64_t bitRankFinder = ((rank + 1) & 0xFF) * PrefixSumMultiplierConstant;
		uint64_t bitRankMask = (cumulativeBits | SignMask) - bitRankFinder;
		int smallerBits = 8 - ((((bitRankMask & SignMask) >> 7) * PrefixSumMultiplierConstant) >> 56);
		assert(smallerBits >= 0);
		assert(smallerBits < 8);
		return smallerBytes * 8 + smallerBits;
	}

	static uint64_t MortonHigh(uint64_t left, uint64_t right)
	{
		return Interleave(left >> 32, right >> 32);
	}

	static uint64_t MortonLow(uint64_t left, uint64_t right)
	{
		return Interleave(left & 0xFFFFFFFF, right & 0xFFFFFFFF);
	}

	//http://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN
	static uint64_t Interleave(uint64_t x, uint64_t y)
	{
		assert(x == (x & 0xFFFFFFFF));
		assert(y == (y & 0xFFFFFFFF));
		static const uint64_t B[] = {0x5555555555555555, 0x3333333333333333, 0x0F0F0F0F0F0F0F0F, 0x00FF00FF00FF00FF, 0x0000FFFF0000FFFF};
		static const uint64_t S[] = {1, 2, 4, 8, 16};

		x = (x | (x << S[4])) & B[4];
		x = (x | (x << S[3])) & B[3];
		x = (x | (x << S[2])) & B[2];
		x = (x | (x << S[1])) & B[1];
		x = (x | (x << S[0])) & B[0];

		y = (y | (y << S[4])) & B[4];
		y = (y | (y << S[3])) & B[3];
		y = (y | (y << S[2])) & B[2];
		y = (y | (y << S[1])) & B[1];
		y = (y | (y << S[0])) & B[0];

		return x | (y << 1);
	}
};

template <typename LengthType, typename ScoreType, typename Word>
class WordSlice
{
public:
	WordSlice() :
	VP(0),
	VN(0),
	scoreEnd(0),
	scoreBeforeStart(0),
	scoreBeforeExists(false),
	sliceExists(false),
	minScore(-1)
#ifdef SLICEVERBOSE
	,debugCalcCount(0)
#endif
	{}
	WordSlice(Word VP, Word VN, ScoreType scoreEnd, ScoreType scoreBeforeStart, bool scoreBeforeExists) :
	VP(VP),
	VN(VN),
	scoreEnd(scoreEnd),
	scoreBeforeStart(scoreBeforeStart),
	scoreBeforeExists(scoreBeforeExists),
	sliceExists(false),
	minScore(-1)
#ifdef SLICEVERBOSE
	,debugCalcCount(0)
#endif
	{}
	Word VP;
	Word VN;
	ScoreType scoreEnd;
	ScoreType scoreBeforeStart;
	ScoreType minScore;
	bool scoreBeforeExists;
	bool sliceExists;
#ifdef SLICEVERBOSE
	int debugCalcCount;
#endif

	void calcMinScore()
	{
		minScore = minScoreLocalMinima();
#ifdef EXTRACORRECTNESSASSERTIONS
		assert(minScore == minScoreCellByCell());
#endif
	}

	ScoreType changedMinScoreInsideBand(const WordSlice& other, const WordSlice& band) const
	{
		if (other.scoreBeforeStart == std::numeric_limits<ScoreType>::max()) return minScoreInsideBand(band);
		auto bandMask = insideBandMask(band);
		auto otherMask = differenceMasks(VP, VN, other.VP, other.VN, other.scoreBeforeStart - scoreBeforeStart);
		auto result = minScoreInsideMask(bandMask & otherMask.first);
		return result;
	}

	ScoreType changedMinScore(WordSlice other) const
	{
		if (other.scoreBeforeStart == std::numeric_limits<ScoreType>::max()) return std::min(scoreBeforeStart, minScore);
		auto result = changedMinScoreLocalMinima(other);
#ifdef EXTRACORRECTNESSASSERTIONS
		assert(result == changedMinScoreCellByCell(other));
#endif
		return result;
	}

	WordSlice mergeWith(const WordSlice& other) const
	{
		auto result = mergeTwoSlices(*this, other);
		return result;
	}

	WordSlice mergeWithVertical(const WordSlice& fullVertical) const
	{
		auto result = mergeWithVerticalSlice(*this, fullVertical);
		return result;
	}

	ScoreType getValue(int row) const
	{
		auto mask = WordConfiguration<Word>::AllOnes;
		if (row < WordConfiguration<Word>::WordSize-1) mask = ~(WordConfiguration<Word>::AllOnes << (row + 1));
		auto value = scoreBeforeStart + WordConfiguration<Word>::popcount(VP & mask) - WordConfiguration<Word>::popcount(VN & mask);
		return value;
	}

	bool smallerOrEqualSomewhere(const WordSlice& band) const
	{
		auto mask = insideBandMask(band);
		return mask != WordConfiguration<Word>::AllZeros;
	}

	bool changedSmallerOrEqualSomewhere(const WordSlice& old, const WordSlice& band) const
	{
		auto changedMask = differenceMasks(VP, VN, old.VP, old.VN, old.scoreBeforeStart - scoreBeforeStart);
		if (changedMask.first == WordConfiguration<Word>::AllZeros) return false;
		auto bandMask = insideBandMask(band);
		auto smallerAndInBand = changedMask.first & bandMask;
		return smallerAndInBand != WordConfiguration<Word>::AllZeros;
	}

	ScoreType changedMinScoreLessThanBand(const WordSlice& old, const WordSlice& min, const WordSlice& band) const
	{
		auto changedMask = differenceMasks(VP, VN, old.VP, old.VN, old.scoreBeforeStart - scoreBeforeStart);
		if (changedMask.first == WordConfiguration<Word>::AllZeros) return false;
		auto bandMask = insideBandMask(band);
		auto mask = changedMask.first & bandMask;

		//rightmost VP between any VN's, aka one cell to the left of a minimum
		Word changePlus = (VP & ~min.VP) | (~VN & min.VN);
		Word changeMinus = (VN & ~min.VN) | (~VP & min.VP);
		Word possibleLocalMinima = (changePlus & (changeMinus - changePlus));
		//shift right by one to get the minimum
		possibleLocalMinima >>= 1;
		//leftmost bit might be a minimum if there is no VP to its right
		possibleLocalMinima |= WordConfiguration<Word>::LastBit & (changeMinus | ~(changeMinus - changePlus)) & ~changePlus;
		//corner cases
		if (mask != WordConfiguration<Word>::AllOnes)
		{
			possibleLocalMinima |= (~mask) >> 1;
			possibleLocalMinima |= (~mask) << 1;
			possibleLocalMinima |= 1;
			possibleLocalMinima |= WordConfiguration<Word>::LastBit;
			possibleLocalMinima &= mask;
		}
		else
		{
			possibleLocalMinima |= 1;
		}
		ScoreType result = std::numeric_limits<ScoreType>::max();
		while (possibleLocalMinima != 0)
		{
			//all cells from the right up to the first minimum are one
			Word currentMinimumMask = possibleLocalMinima ^ (possibleLocalMinima-1);
			ScoreType scoreHere = scoreBeforeStart + WordConfiguration<Word>::popcount(VP & currentMinimumMask) - WordConfiguration<Word>::popcount(VN & currentMinimumMask);
			ScoreType minScoreHere = min.scoreBeforeStart + WordConfiguration<Word>::popcount(min.VP & currentMinimumMask) - WordConfiguration<Word>::popcount(min.VN & currentMinimumMask);
			result = std::min(result, scoreHere - minScoreHere);
			possibleLocalMinima &= ~currentMinimumMask;
		}
		return result;
	}

private:

	Word insideBandMask(const WordSlice& band) const
	{
		if (scoreBeforeStart > band.scoreBeforeStart)
		{
			auto bandMask = differenceMasks(band.VP, band.VN, VP, VN, scoreBeforeStart - band.scoreBeforeStart);
			return ~bandMask.first;
		}
		auto bandMask = differenceMasks(VP, VN, band.VP, band.VN, band.scoreBeforeStart - scoreBeforeStart);
		return ~bandMask.second;
	}

#ifdef EXTRACORRECTNESSASSERTIONS
	ScoreType changedMinScoreCellByCell(WordSlice oldSlice) const
	{
		ScoreType oldScore = oldSlice.scoreBeforeStart;
		ScoreType newScore = scoreBeforeStart;
		Word mask = 1;
		ScoreType result = std::numeric_limits<ScoreType>::max();
		if (newScore < oldScore) result = std::min(result, newScore);
		for (int i = 0; i < WordConfiguration<Word>::WordSize; i++)
		{
			oldScore += (oldSlice.VP & mask) ? 1 : 0;
			oldScore -= (oldSlice.VN & mask) ? 1 : 0;
			newScore += (VP & mask) ? 1 : 0;
			newScore -= (VN & mask) ? 1 : 0;
			if (newScore < oldScore) result = std::min(result, newScore);
			mask <<= 1;
		}
		return result;
	}

	ScoreType minScoreCellByCell() const
	{
		ScoreType minScore = std::numeric_limits<ScoreType>::max();
		ScoreType scoreHere = scoreBeforeStart;
		for (int i = 0; i < WordConfiguration<Word>::WordSize; i++)
		{
			Word mask = ((Word)1) << i;
			scoreHere += (VP & mask) ? 1 : 0;
			scoreHere -= (VN & mask) ? 1 : 0;
			minScore = std::min(minScore, scoreHere);
		}
		return minScore;
	}

	ScoreType minScoreInsideBandCellbycell(const WordSlice& band) const
	{
		ScoreType result = std::numeric_limits<ScoreType>::max();
		for (int i = 0; i < WordConfiguration<Word>::WordSize; i++)
		{
			if (getValue(i) <= band.getValue(i))
			{
				result = std::min(result, getValue(i));
			}
		}
		return result;
	}
#endif

	ScoreType minScoreInsideBand(const WordSlice& band) const
	{
		auto result = minScoreInsideBandLocalMinima(band);
#ifdef EXTRACORRECTNESSASSERTIONS
		assert(result == minScoreInsideBandCellbycell(band));
#endif
		return result;
	}

	ScoreType minScoreInsideBandLocalMinima(const WordSlice& band) const
	{
		auto mask = insideBandMask(band);
		ScoreType result = minScoreInsideMask(mask);
		return result;
	}

	ScoreType minScoreLocalMinima() const
	{
		//rightmost VP between any VN's, aka one cell to the left of a minimum
		Word possibleLocalMinima = (VP & (VN - VP));
		//shift right by one to get the minimum
		possibleLocalMinima >>= 1;
		//leftmost bit might be a minimum if there is no VP to its right
		possibleLocalMinima |= WordConfiguration<Word>::LastBit & (VN | ~(VN - VP)) & ~VP;
		ScoreType result = scoreBeforeStart + (VP & 1) - (VN & 1);
		//the score is inited to the first cell at the start
		possibleLocalMinima &= ~((Word)1);
		while (possibleLocalMinima != 0)
		{
			//all cells from the right up to the first minimum are one
			Word currentMinimumMask = possibleLocalMinima ^ (possibleLocalMinima-1);
			ScoreType scoreHere = scoreBeforeStart + WordConfiguration<Word>::popcount(VP & currentMinimumMask) - WordConfiguration<Word>::popcount(VN & currentMinimumMask);
			result = std::min(result, scoreHere);
			possibleLocalMinima &= ~currentMinimumMask;
		}
		return result;
	}

	ScoreType changedMinScoreLocalMinima(WordSlice oldSlice) const
	{
		auto masks = differenceMasks(VP, VN, oldSlice.VP, oldSlice.VN, oldSlice.scoreBeforeStart - scoreBeforeStart);
		Word smaller = masks.first;
		ScoreType result = minScoreInsideMask(smaller);
		if (scoreBeforeStart < oldSlice.scoreBeforeStart) result = std::min(result, scoreBeforeStart);
		return result;
	}

	ScoreType minScoreInsideMask(Word mask) const
	{
		//rightmost VP between any VN's, aka one cell to the left of a minimum
		Word possibleLocalMinima = (VP & (VN - VP));
		//shift right by one to get the minimum
		possibleLocalMinima >>= 1;
		//leftmost bit might be a minimum if there is no VP to its right
		possibleLocalMinima |= WordConfiguration<Word>::LastBit & (VN | ~(VN - VP)) & ~VP;
		//corner cases
		if (mask != WordConfiguration<Word>::AllOnes)
		{
			possibleLocalMinima |= (~mask) >> 1;
			possibleLocalMinima |= (~mask) << 1;
			possibleLocalMinima |= 1;
			possibleLocalMinima |= WordConfiguration<Word>::LastBit;
			possibleLocalMinima &= mask;
		}
		else
		{
			possibleLocalMinima |= 1;
		}
		ScoreType result = std::numeric_limits<ScoreType>::max();
		while (possibleLocalMinima != 0)
		{
			//all cells from the right up to the first minimum are one
			Word currentMinimumMask = possibleLocalMinima ^ (possibleLocalMinima-1);
			ScoreType scoreHere = scoreBeforeStart + WordConfiguration<Word>::popcount(VP & currentMinimumMask) - WordConfiguration<Word>::popcount(VN & currentMinimumMask);
			result = std::min(result, scoreHere);
			possibleLocalMinima &= ~currentMinimumMask;
		}
		return result;
	}

	ScoreType minScoreLogW() const
	{
		if (VP == WordConfiguration<Word>::AllOnes) return scoreBeforeStart + 1;
		if (VN == WordConfiguration<Word>::AllOnes) return scoreEnd;
		Word tmpVP = VP;
		Word tmpVN = VN;
		uint64_t chunkValue = 0x1010101010101010 + (tmpVP & WordConfiguration<Word>::LSBMask) - (tmpVN & WordConfiguration<Word>::LSBMask);
		uint64_t chunkMin = chunkValue;
		for (int i = 1; i < 8; i++)
		{
			tmpVP >>= 1;
			tmpVN >>= 1;
			chunkValue += tmpVP & WordConfiguration<Word>::LSBMask;
			chunkValue -= tmpVN & WordConfiguration<Word>::LSBMask;
			assert((chunkValue & 0x2020202020202020) == 0);
			uint64_t diff = 0x2020202020202020 + chunkMin - chunkValue;
			uint64_t smallerChunk = ((diff & 0x2020202020202020) >> 5) * 0x0F;
			chunkMin = chunkMin - (diff & smallerChunk);
		}
		Word VPVN = byteVPVNSum(bytePrefixSums(WordConfiguration<Word>::ChunkPopcounts(VP), 0), bytePrefixSums(WordConfiguration<Word>::ChunkPopcounts(VN), 0));
		ScoreType result = (chunkMin & 0xFF) - 16;
		for (int i = 1; i < 8; i++)
		{
			VPVN >>= 8;
			chunkMin >>= 8;
			ScoreType value = (chunkMin & 0xFF) - 16 + (VPVN & 0x7F) - (VPVN & 0x80);
			result = std::min(result, value);
		}
		return result + scoreBeforeStart;
	}

	static uint64_t bytePrefixSums(uint64_t value, int addition)
	{
		value <<= WordConfiguration<Word>::ChunkBits;
		assert(addition >= 0);
		value += addition;
		return value * WordConfiguration<Word>::PrefixSumMultiplierConstant;
	}

	static uint64_t bytePrefixSums(uint64_t value)
	{
		value <<= WordConfiguration<Word>::ChunkBits;
		return value * WordConfiguration<Word>::PrefixSumMultiplierConstant;
	}

	static uint64_t byteVPVNSum(uint64_t prefixSumVP, uint64_t prefixSumVN)
	{
		uint64_t result = WordConfiguration<Word>::SignMask;
		assert((prefixSumVP & result) == 0);
		assert((prefixSumVN & result) == 0);
		result += prefixSumVP;
		result -= prefixSumVN;
		result ^= WordConfiguration<Word>::SignMask;
		return result;
	}

	static WordSlice mergeWithVerticalSlice(WordSlice slice, WordSlice fullVertical)
	{
		assert(fullVertical.VP == WordConfiguration<Word>::AllOnes);
		assert(slice.scoreBeforeStart > fullVertical.scoreBeforeStart);
		if (slice.scoreEnd >= fullVertical.scoreEnd)
		{
			return fullVertical;
		}
		auto masks = differenceMaskFullVertical(slice, fullVertical.scoreBeforeStart);
#ifdef EXTRACORRECTNESSASSERTIONS
		auto debugCompare = differenceMasks(fullVertical.VP, fullVertical.VN, slice.VP, slice.VN, slice.scoreBeforeStart - fullVertical.scoreBeforeStart);
		assert(masks.first == debugCompare.first);
		assert(masks.second == debugCompare.second);
#endif
		return mergeTwoSlices(fullVertical, slice, masks.first, masks.second);
	}

	static std::pair<Word, Word> differenceMaskFullVertical(WordSlice slice, ScoreType fullVerticalScore)
	{
		Word low = ~slice.VP;
		Word high = slice.VN;
		Word mortonLow = WordConfiguration<Word>::MortonLow(high, low);
		Word mortonHigh = WordConfiguration<Word>::MortonHigh(high, low);
		auto pos = WordConfiguration<Word>::BitPosition(mortonLow, mortonHigh, slice.scoreBeforeStart - fullVerticalScore - 1);
		auto nextpos = WordConfiguration<Word>::BitPosition(mortonLow, mortonHigh, slice.scoreBeforeStart - fullVerticalScore);
		auto bitpos = pos / 2;
		auto nextbitpos = nextpos / 2;
		auto verticalSmaller = ~(WordConfiguration<Word>::AllOnes << bitpos);
		auto sliceSmaller = WordConfiguration<Word>::AllOnes << nextbitpos;
		return std::make_pair(verticalSmaller, sliceSmaller);
	}

	static WordSlice mergeTwoSlices(WordSlice left, WordSlice right)
	{
		//O(log w), because prefix sums need log w chunks of log w bits
		static_assert(std::is_same<Word, uint64_t>::value);
		if (left.scoreBeforeStart > right.scoreBeforeStart) std::swap(left, right);
		assert((left.VP & left.VN) == WordConfiguration<Word>::AllZeros);
		assert((right.VP & right.VN) == WordConfiguration<Word>::AllZeros);
		auto masks = differenceMasks(left.VP, left.VN, right.VP, right.VN, right.scoreBeforeStart - left.scoreBeforeStart);
		return mergeTwoSlices(left, right, masks.first, masks.second);
	}

	static WordSlice mergeTwoSlices(WordSlice left, WordSlice right, Word leftSmaller, Word rightSmaller)
	{
		assert(left.scoreBeforeStart <= right.scoreBeforeStart);
		WordSlice result;
		result.sliceExists = true;
		assert((left.VP & left.VN) == WordConfiguration<Word>::AllZeros);
		assert((right.VP & right.VN) == WordConfiguration<Word>::AllZeros);
		assert((leftSmaller & rightSmaller) == 0);
		auto mask = (rightSmaller | ((leftSmaller | rightSmaller) - (rightSmaller << 1))) & ~leftSmaller;
		uint64_t leftReduction = leftSmaller & (rightSmaller << 1);
		uint64_t rightReduction = rightSmaller & (leftSmaller << 1);
		if ((rightSmaller & 1) && left.scoreBeforeStart < right.scoreBeforeStart)
		{
			rightReduction |= 1;
		}
		assert((leftReduction & right.VP) == leftReduction);
		assert((rightReduction & left.VP) == rightReduction);
		assert((leftReduction & left.VN) == leftReduction);
		assert((rightReduction & right.VN) == rightReduction);
		left.VN &= ~leftReduction;
		right.VN &= ~rightReduction;
		result.VN = (left.VN & ~mask) | (right.VN & mask);
		result.VP = (left.VP & ~mask) | (right.VP & mask);
		assert((result.VP & result.VN) == 0);
		result.scoreBeforeStart = left.scoreBeforeStart;
		result.scoreEnd = std::min(left.scoreEnd, right.scoreEnd);
		result.scoreBeforeExists = left.scoreBeforeExists;
		if (right.scoreBeforeStart == left.scoreBeforeStart)
		{
			result.scoreBeforeExists |= right.scoreBeforeExists;
		}
		assert(result.scoreEnd == result.scoreBeforeStart + WordConfiguration<Word>::popcount(result.VP) - WordConfiguration<Word>::popcount(result.VN));
		return result;
	}

	static std::pair<uint64_t, uint64_t> differenceMasks(uint64_t leftVP, uint64_t leftVN, uint64_t rightVP, uint64_t rightVN, int scoreDifference)
	{
		auto result = differenceMasksBytePrecalc(leftVP, leftVN, rightVP, rightVN, scoreDifference);
#ifdef EXTRACORRECTNESSASSERTIONS
		auto debugCompare = differenceMasksWord(leftVP, leftVN, rightVP, rightVN, scoreDifference);
		assert(result.first == debugCompare.first);
		assert(result.second == debugCompare.second);
#endif
		return result;
	}

	static ScoreType clamp(ScoreType low, ScoreType val, ScoreType high)
	{
		return std::min(high, std::max(low, val));
	}

	__attribute__((optimize("unroll-loops")))
	static std::pair<Word, Word> differenceMasksBytePrecalc(Word leftVP, Word leftVN, Word rightVP, Word rightVN, int scoreDifference)
	{
		assert(scoreDifference >= 0);
		Word VPcommon = ~(leftVP & rightVP);
		Word VNcommon = ~(leftVN & rightVN);
		leftVP &= VPcommon;
		leftVN &= VNcommon;
		rightVP &= VPcommon;
		rightVN &= VNcommon;
		Word twosmaller = leftVN & rightVP;
		Word onesmaller = (rightVP & ~leftVN) | (leftVN & ~rightVP);
		Word equal = ~leftVP & ~leftVN & ~rightVP & ~rightVN;
		Word onebigger = (leftVP & ~rightVN) | (rightVN & ~leftVP);
		Word twobigger = rightVN & leftVP;
		Word sign = onesmaller | twosmaller;
		Word low = ~equal;
		Word high = twosmaller | twobigger;
		Word leftSmaller = 0;
		Word rightSmaller = 0;
		for (size_t i = 0; i < sizeof(Word); i++)
		{
			if (scoreDifference >= 0)
			{
				std::tuple<uint8_t, uint8_t, int8_t> bytePrecalced = ByteStuff::precalcedVPVNChanges[((size_t)(std::min(scoreDifference, 17)) << 24) + ((sign & 0xFF) << 16) + ((low & 0xFF) << 8) + (high & 0xFF)];
				leftSmaller |= ((Word)std::get<0>(bytePrecalced)) << (i * 8);
				rightSmaller |= ((Word)std::get<1>(bytePrecalced)) << (i * 8);
				scoreDifference += std::get<2>(bytePrecalced);
			}
			else
			{
				std::tuple<uint8_t, uint8_t, int8_t> bytePrecalced = ByteStuff::precalcedVPVNChanges[((size_t)(std::min(-scoreDifference, 17)) << 24) + ((~sign & 0xFF) << 16) + ((low & 0xFF) << 8) + (high & 0xFF)];
				leftSmaller |= ((Word)std::get<1>(bytePrecalced)) << (i * 8);
				rightSmaller |= ((Word)std::get<0>(bytePrecalced)) << (i * 8);
				scoreDifference -= std::get<2>(bytePrecalced);
			}
			sign >>= 8;
			low >>= 8;
			high >>= 8;
		}
		return std::make_pair(leftSmaller, rightSmaller);
	}

	static std::pair<uint64_t, uint64_t> differenceMasksWord(uint64_t leftVP, uint64_t leftVN, uint64_t rightVP, uint64_t rightVN, int scoreDifference)
	{
		assert(scoreDifference >= 0);
		const uint64_t signmask = WordConfiguration<Word>::SignMask;
		const uint64_t lsbmask = WordConfiguration<Word>::LSBMask;
		const int chunksize = WordConfiguration<Word>::ChunkBits;
		const uint64_t allones = WordConfiguration<Word>::AllOnes;
		const uint64_t allzeros = WordConfiguration<Word>::AllZeros;
		uint64_t VPcommon = ~(leftVP & rightVP);
		uint64_t VNcommon = ~(leftVN & rightVN);
		leftVP &= VPcommon;
		leftVN &= VNcommon;
		rightVP &= VPcommon;
		rightVN &= VNcommon;
		//left is lower everywhere
		if (scoreDifference > WordConfiguration<Word>::popcount(rightVN) + WordConfiguration<Word>::popcount(leftVP))
		{
			return std::make_pair(allones, allzeros);
		}
		if (scoreDifference == 128 && rightVN == allones && leftVP == allones)
		{
			return std::make_pair(allones ^ ((Word)1 << (WordConfiguration<Word>::WordSize-1)), allzeros);
		}
		else if (scoreDifference == 0 && rightVN == allones && leftVP == allones)
		{
			return std::make_pair(0, allones);
		}
		assert(scoreDifference >= 0);
		assert(scoreDifference < 128);
		uint64_t byteVPVNSumLeft = byteVPVNSum(bytePrefixSums(WordConfiguration<Word>::ChunkPopcounts(leftVP), 0), bytePrefixSums(WordConfiguration<Word>::ChunkPopcounts(leftVN), 0));
		uint64_t byteVPVNSumRight = byteVPVNSum(bytePrefixSums(WordConfiguration<Word>::ChunkPopcounts(rightVP), scoreDifference), bytePrefixSums(WordConfiguration<Word>::ChunkPopcounts(rightVN), 0));
		uint64_t difference = byteVPVNSumLeft;
		{
			//take the bytvpvnsumright and split it from positive/negative values into two vectors with positive values, one which needs to be added and the other deducted
			//smearmask is 1 where the number needs to be deducted, and 0 where it needs to be added
			//except sign bits which are all 0
			uint64_t smearmask = ((byteVPVNSumRight & signmask) >> (chunksize-1)) * ((((Word)1) << (chunksize-1))-1);
			assert((smearmask & signmask) == 0);
			uint64_t deductions = ~smearmask & byteVPVNSumRight & ~signmask;
			//byteVPVNSumRight is in one's complement so take the not-value + 1
			uint64_t additions = (smearmask & ~byteVPVNSumRight) + (smearmask & lsbmask);
			assert((deductions & signmask) == 0);
			uint64_t signsBefore = difference & signmask;
			//unset the sign bits so additions don't interfere with other chunks
			difference &= ~signmask;
			difference += additions;
			//the sign bit is 1 if the value went from <0 to >=0
			//so in that case we need to flip it
			difference ^= signsBefore;
			signsBefore = difference & signmask;
			//set the sign bits so that deductions don't interfere with other chunks
			difference |= signmask;
			difference -= deductions;
			//sign bit is 0 if the value went from >=0 to <0
			//so flip them to the correct values
			signsBefore ^= signmask & ~difference;
			difference &= ~signmask;
			difference |= signsBefore;
		}
		//difference now contains the prefix sum difference (left-right) at each chunk
		uint64_t resultLeftSmallerThanRight = 0;
		uint64_t resultRightSmallerThanLeft = 0;
		for (int bit = 0; bit < chunksize; bit++)
		{
			uint64_t signsBefore = difference & signmask;
			//unset the sign bits so additions don't interfere with other chunks
			difference &= ~signmask;
			difference += leftVP & lsbmask;
			difference += rightVN & lsbmask;
			//the sign bit is 1 if the value went from <0 to >=0
			//so in that case we need to flip it
			difference ^= signsBefore;
			signsBefore = difference & signmask;
			//set the sign bits so that deductions don't interfere with other chunks
			difference |= signmask;
			difference -= leftVN & lsbmask;
			difference -= rightVP & lsbmask;
			//sign bit is 0 if the value went from >=0 to <0
			//so flip them to the correct values
			signsBefore ^= signmask & ~difference;
			difference &= ~signmask;
			difference |= signsBefore;
			leftVN >>= 1;
			leftVP >>= 1;
			rightVN >>= 1;
			rightVP >>= 1;
			//difference now contains the prefix sums difference (left-right) at each byte at (bit)'th bit
			//left < right when the prefix sum difference is negative (sign bit is set)
			uint64_t negative = (difference & signmask);
			resultLeftSmallerThanRight |= negative >> (WordConfiguration<Word>::ChunkBits - 1 - bit);
			//Test equality to zero. If it's zero, substracting one will make the sign bit 0, otherwise 1
			uint64_t notEqualToZero = ((difference | signmask) - lsbmask) & signmask;
			//right > left when the prefix sum difference is positive (not zero and not negative)
			resultRightSmallerThanLeft |= (notEqualToZero & ~negative) >> (WordConfiguration<Word>::ChunkBits - 1 - bit);
		}
		return std::make_pair(resultLeftSmallerThanRight, resultRightSmallerThanLeft);
	}

};

#endif