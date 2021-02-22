//===------------ PatternSet.h - Unordered set of code patterns -----------===//
//
//       SimpLL - Program simplifier for analysis of semantic difference      //
//
// This file is published under Apache 2.0 license. See LICENSE for details.
// Author: Petr Silling, psilling@redhat.com
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the unordered LLVM code pattern set.
///
//===----------------------------------------------------------------------===//

#ifndef DIFFKEMP_SIMPLL_PATTERNSET_H
#define DIFFKEMP_SIMPLL_PATTERNSET_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/IR/Module.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace llvm;

/// Instruction to instruction mapping.
typedef DenseMap<const Instruction *, const Instruction *> InstructionMap;

/// Instructions pointer set.
typedef SmallPtrSet<const Instruction *, 32> InstructionSet;

// Forward declaration of the DifferentialFunctionComparator.
class DifferentialFunctionComparator;

/// Representation of difference pattern metadata configuration.
struct PatternMetadata {
    /// Limit for the number of following basic blocks.
    int BasicBlockLimit = -1;
    /// End of the previous basic block limit.
    bool BasicBlockLimitEnd = false;
    /// Marker for the first differing instruction pair.
    bool PatternStart = false;
    /// Marker for the last differing instruction pair.
    bool PatternEnd = false;
};

/// Representation of the whole difference pattern configuration.
struct PatternConfiguration {
    /// Logging option for parse failures.
    std::string OnParseFailure;
    /// Vector of paths to pattern files.
    std::vector<std::string> PatternFiles;
};

/// Representation of a difference pattern pair.
struct Pattern {
    /// Name of the pattern.
    const std::string Name;
    /// Function corresponding to the new part of the pattern.
    const Function *NewPattern;
    /// Function corresponding to the old part of the pattern.
    const Function *OldPattern;
    /// Map of all included pattern metadata.
    mutable std::unordered_map<const Instruction *, PatternMetadata>
            MetadataMap;
    /// Final instruction mapping associated with the pattern.
    mutable InstructionMap FinalMapping;
    /// Comparison start position for the new part of the pattern.
    const Instruction *NewStartPosition = nullptr;
    /// Comparison start position for the old part of the pattern.
    const Instruction *OldStartPosition = nullptr;

    Pattern(const std::string &Name,
            const Function *NewPattern,
            const Function *OldPattern)
            : Name(Name), NewPattern(NewPattern), OldPattern(OldPattern) {}

    bool operator==(const Pattern &Rhs) const {
        return (Name == Rhs.Name && NewPattern == Rhs.NewPattern
                && OldPattern == Rhs.OldPattern);
    }
};

// Define a hash function for difference patterns.
namespace std {
template <> struct hash<Pattern> {
    std::size_t operator()(const Pattern &Pat) const noexcept {
        return std::hash<std::string>()(Pat.Name);
    }
};
} // namespace std

/// Compares difference patterns against functions, possibly eliminating reports
/// of prior semantic differences.
class PatternSet {
  public:
    /// Name for the function defining final instuction mapping.
    static const std::string MappingFunctionName;
    /// Name for pattern metadata nodes.
    static const std::string MetadataName;
    /// Prefix for the new side of difference patterns.
    static const std::string NewPrefix;
    /// Prefix for the old side of difference patterns.
    static const std::string OldPrefix;

    PatternSet(std::string ConfigPath);

    ~PatternSet();

    /// Retrives pattern metadata attached to the given instruction, returning
    /// true for valid pattern metadata nodes.
    bool getPatternMetadata(PatternMetadata &Metadata,
                            const Instruction &Inst) const;

    /// Checks whether the difference pattern set is empty.
    bool empty() const noexcept { return Patterns.empty(); }

    /// Returns a constant iterator pointing to the first difference pattern.
    std::unordered_set<Pattern>::iterator begin() noexcept {
        return Patterns.begin();
    }

    /// Returns a constant iterator pointing beyond the last difference pattern.
    std::unordered_set<Pattern>::iterator end() noexcept {
        return Patterns.end();
    }

    /// Returns a constant iterator pointing to the first difference pattern.
    std::unordered_set<Pattern>::const_iterator begin() const noexcept {
        return Patterns.begin();
    }

    /// Returns a constant iterator pointing beyond the last difference pattern.
    std::unordered_set<Pattern>::const_iterator end() const noexcept {
        return Patterns.end();
    }

    /// Returns a constant iterator pointing to the first difference pattern.
    std::unordered_set<Pattern>::const_iterator cbegin() const noexcept {
        return Patterns.cbegin();
    }

    /// Returns a constant iterator pointing beyond the last difference pattern.
    std::unordered_set<Pattern>::const_iterator cend() const noexcept {
        return Patterns.cend();
    }

  private:
    /// Basic information about the final instruction mapping present on one
    /// side of a pattern.
    using MappingInfo = std::pair<const Instruction *, int>;

    /// Settings applied to all pattern files.
    StringMap<std::string> GlobalSettings;
    /// Map of loaded pattern modules.
    std::unordered_map<Module *, std::unique_ptr<Module>> PatternModules;
    /// Map of loaded pattern module contexts.
    std::unordered_map<Module *, std::unique_ptr<LLVMContext>> PatternContexts;
    /// Set of loaded difference patterns.
    std::unordered_set<Pattern> Patterns;

    /// Load the given configuration file.
    void loadConfig(std::string &ConfigPath);

    /// Add a new difference pattern.
    void addPattern(std::string &Path);

    /// Initializes a pattern, loading all metadata, start positions, and the
    /// final instruction mapping.
    bool initializePattern(Pattern &Pat);

    /// Initializes a single side of a pattern, loading all metadata, start
    /// positions, and retrevies instruction mapping information.
    void initializePatternSide(Pattern &Pat,
                               MappingInfo &MapInfo,
                               bool IsNewSide);

    /// Parses a single pattern metadata operand, including all dependent
    /// operands.
    int parseMetadataOperand(PatternMetadata &PatternMetadata,
                             const MDNode *InstMetadata,
                             const int Index) const;
};

#endif // DIFFKEMP_SIMPLL_PATTERNSET_H
