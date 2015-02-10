/*
 * Open Chinese Convert
 *
 * Copyright 2015 BYVoid <byvoid@byvoid.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cmath>

#include "PhraseExtract.hpp"

namespace opencc {
namespace internal {

template <typename VAL_TYPE>
VAL_TYPE Lookup(const std::unordered_map<UTF8StringSlice, VAL_TYPE,
                                         UTF8StringSlice::Hasher>& dict,
                const UTF8StringSlice& wordCandidate) {
  const auto& iterator = dict.find(wordCandidate);
  if (iterator != dict.end()) {
    return iterator->second;
  } else {
    assert(false);
  }
}

bool ContainsPunctuation(const UTF8StringSlice& word) {
  static const vector<UTF8StringSlice> punctuations = {
      " ",  "\n", "\r", "\t", "-",  ",",  ".",  "?",  "!", "*",
      "　", "，", "。", "、", "；", "：", "？", "！", "…", "“",
      "”",  "「", "」", "—",  "－", "（", "）", "《", "》"};
  for (const auto& punctuation : punctuations) {
    if (word.FindBytePosition(punctuation) != static_cast<size_t>(-1)) {
      return true;
    }
  }
  return false;
}

bool NoFilter(const UTF8StringSlice&) { return false; }

} // namespace internal

using namespace internal;

PhraseExtract::PhraseExtract()
    : wordMaxLength(2), prefixSetLength(1), suffixSetLength(1),
      preCalculationFilter(NoFilter), postCalculationFilter(NoFilter),
      utf8FullText("") {}

void PhraseExtract::Detect(const string& fullText) {
  Reset();
  SetFullText(fullText);
  ExtractSuffixes();
  ExtractPrefixes();
  CalculateFrequency();
  ExtractWordCandidates();
  CalculateCohesions();
  CalculateSuffixEntropy();
  CalculatePrefixEntropy();
  SelectWords();
}

void PhraseExtract::Reset() {
  totalOccurrence = 0;
  logTotalOccurrence = 0;
  prefixes.clear();
  suffixes.clear();
  wordCandidates.clear();
  words.clear();
  frequencies.clear();
  cohesions.clear();
  suffixEntropies.clear();
  prefixEntropies.clear();
  utf8FullText = UTF8StringSlice("");
}

void PhraseExtract::ExtractSuffixes() {
  for (UTF8StringSlice text = utf8FullText; text.UTF8Length() > 0;
       text.MoveRight()) {
    size_t suffixLength =
        std::min(wordMaxLength + suffixSetLength, text.UTF8Length());
    suffixes.push_back(text.Left(suffixLength));
  }
  // Sort suffixes
  std::sort(suffixes.begin(), suffixes.end());
}

void PhraseExtract::ExtractPrefixes() {
  for (UTF8StringSlice text = utf8FullText; text.UTF8Length() > 0;
       text.MoveLeft()) {
    size_t prefixLength =
        std::min(wordMaxLength + prefixSetLength, text.UTF8Length());
    prefixes.push_back(text.Right(prefixLength));
  }
  // Sort suffixes reversely
  std::sort(prefixes.begin(), prefixes.end(),
            [](const UTF8StringSlice& a, const UTF8StringSlice& b) {
    return a.ReverseCompare(b) < 0;
  });
}

void PhraseExtract::CalculateFrequency() {
  for (const auto& suffix : suffixes) {
    for (size_t i = 1; i <= suffix.UTF8Length() && i <= wordMaxLength; i++) {
      const UTF8StringSlice wordCandidate = suffix.Left(i);
      frequencies[wordCandidate]++;
      totalOccurrence++;
    }
  }
  logTotalOccurrence = log(totalOccurrence);
}

void PhraseExtract::ExtractWordCandidates() {
  for (const auto& item : frequencies) {
    const UTF8StringSlice wordCandidate = item.first;
    if (ContainsPunctuation(wordCandidate)) {
      continue;
    }
    if (!preCalculationFilter(wordCandidate)) {
      wordCandidates.push_back(wordCandidate);
    }
  }
  // Sort by frequency
  std::sort(wordCandidates.begin(), wordCandidates.end(),
            [this](const UTF8StringSlice& a, const UTF8StringSlice& b) {
    return Frequency(a) > Frequency(b);
  });
}

void PhraseExtract::CalculateSuffixEntropy() {
  for (size_t length = 1; length <= wordMaxLength; length++) {
    std::unordered_map<UTF8StringSlice, size_t, UTF8StringSlice::Hasher>
        suffixSet;
    UTF8StringSlice lastWord("");
    const auto& updateEntropy = [this, &suffixSet, &lastWord]() {
      if (lastWord.UTF8Length() > 0) {
        suffixEntropies[lastWord] = CalculateEntropy(suffixSet);
        suffixSet.clear();
      }
    };
    for (const auto& suffix : suffixes) {
      if (suffix.UTF8Length() < length) {
        continue;
      }
      const auto& wordCandidate = suffix.Left(length);
      if (wordCandidate != lastWord) {
        updateEntropy();
        lastWord = wordCandidate;
      }
      if (length + suffixSetLength <= suffix.UTF8Length()) {
        const auto& wordSuffix = suffix.SubString(length, suffixSetLength);
        suffixSet[wordSuffix]++;
      }
    }
    updateEntropy();
  }
}

void PhraseExtract::CalculatePrefixEntropy() {
  for (size_t length = 1; length <= wordMaxLength; length++) {
    std::unordered_map<UTF8StringSlice, size_t, UTF8StringSlice::Hasher>
        prefixSet;
    UTF8StringSlice lastWord("");
    const auto& updateEntropy = [this, &prefixSet, &lastWord]() {
      if (lastWord.UTF8Length() > 0) {
        prefixEntropies[lastWord] = CalculateEntropy(prefixSet);
        prefixSet.clear();
      }
    };
    for (const auto& prefix : prefixes) {
      if (prefix.UTF8Length() < length) {
        continue;
      }
      const auto& wordCandidate = prefix.Right(length);
      if (wordCandidate != lastWord) {
        updateEntropy();
        lastWord = wordCandidate;
      }
      if (length + prefixSetLength <= prefix.UTF8Length()) {
        const auto& wordPrefix = prefix.SubString(
            prefix.UTF8Length() - length - prefixSetLength, prefixSetLength);
        prefixSet[wordPrefix]++;
      }
    }
    updateEntropy();
  }
}

void PhraseExtract::CalculateCohesions() {
  for (const auto& wordCandidate : wordCandidates) {
    cohesions[wordCandidate] = CalculateCohesion(wordCandidate);
  }
}

double PhraseExtract::Cohesion(const UTF8StringSlice& word) const {
  return Lookup(cohesions, word);
}

double PhraseExtract::Entropy(const UTF8StringSlice& word) const {
  return SuffixEntropy(word) + PrefixEntropy(word);
}

double PhraseExtract::SuffixEntropy(const UTF8StringSlice& word) const {
  return Lookup(suffixEntropies, word);
}

double PhraseExtract::PrefixEntropy(const UTF8StringSlice& word) const {
  return Lookup(prefixEntropies, word);
}

size_t PhraseExtract::Frequency(const UTF8StringSlice& word) const {
  const size_t frequency = Lookup(frequencies, word);
  return frequency;
}

double PhraseExtract::LogProbability(const UTF8StringSlice& word) const {
  // log(frequency / totalOccurrence) = log(frequency) - log(totalOccurrence)
  const size_t frequency = Lookup(frequencies, word);
  return log(frequency) - logTotalOccurrence;
}

double PhraseExtract::PMI(const UTF8StringSlice& wordCandidate,
                          const UTF8StringSlice& part1,
                          const UTF8StringSlice& part2) const {
  // PMI(x, y) = log(P(x, y) / (P(x) * P(y)))
  //           = log(P(x, y)) - log(P(x)) - log(P(y))
  return LogProbability(wordCandidate) - LogProbability(part1) -
         LogProbability(part2);
}

double
PhraseExtract::CalculateCohesion(const UTF8StringSlice& wordCandidate) const {
  // TODO Try average value
  double minPMI = INFINITY;
  for (size_t leftLength = 1; leftLength <= wordCandidate.UTF8Length() - 1;
       leftLength++) {
    const auto& leftPart = wordCandidate.Left(leftLength);
    const auto& rightPart =
        wordCandidate.Right(wordCandidate.UTF8Length() - leftLength);
    double pmi = PMI(wordCandidate, leftPart, rightPart);
    minPMI = std::min(pmi, minPMI);
  }
  return minPMI;
}

double PhraseExtract::CalculateEntropy(const std::unordered_map<
    UTF8StringSlice, size_t, UTF8StringSlice::Hasher>& choices) const {
  double totalChoices = 0;
  for (const auto& item : choices) {
    totalChoices += item.second;
  }
  double entropy = 0;
  for (const auto& item : choices) {
    const size_t occurrence = item.second;
    const double probability = occurrence / totalChoices;
    entropy += probability * log(probability);
  }
  if (entropy != 0) {
    entropy = -entropy;
  }
  return entropy;
}

void PhraseExtract::SelectWords() {
  for (const auto& word : wordCandidates) {
    if (!postCalculationFilter(word)) {
      words.push_back(word);
    }
  }
}

} // namespace opencc
