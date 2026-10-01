/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: core script virtual machine
 */

#ifndef ZEROCOMICO_SCRIPT_VM_H
#define ZEROCOMICO_SCRIPT_VM_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/random.h"
#include "common/serializer.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace ZeroComico {

class ScriptProgram;
struct ScriptInstruction;

class ScriptVMHost {
public:
	virtual ~ScriptVMHost() = default;

	// Return true when the opcode was handled. Unknown game-specific opcodes
	// can safely be ignored by a caller that is only executing a bootstrap
	// fragment, but the full runtime will eventually implement them here.
	virtual bool executeScriptOpcode(const ScriptInstruction &instruction) = 0;

	// Game-specific conditionals still use the VM's branch machinery, but
	// their truth value comes from runtime state such as inventory selection.
	virtual bool evaluateScriptCondition(const ScriptInstruction &instruction,
	                                   bool &result) const {
		return false;
	}

	// wjmp is the retail script scheduler's yield-and-jump primitive. A native
	// host uses this hook to pump input and advance one script tick before the
	// VM resumes at the requested label.
	virtual bool yieldScriptExecution() { return true; }

	// Retail begin_thread blocks containing scheduler jumps run asynchronously.
	// The host owns their lifetime because ScriptProgram storage belongs to the
	// active room/puzzle/character subsystem.
	virtual bool scheduleScriptThread(const ScriptProgram &program,
	                                  uint32 startIndex, uint32 endIndex) {
		(void)program;
		(void)startIndex;
		(void)endIndex;
		return false;
	}
};

class ScriptVM {
public:
	explicit ScriptVM(ScriptVMHost *host);

	void reset();

	bool run(const ScriptProgram &program, uint32 startIndex = 0,
	         uint32 endIndex = 0xffffffffU, uint32 maxSteps = 100000);

	// Execute one asynchronous thread until it reaches a retail scheduler
	// boundary (wjmp/wjmp_if_*), finishes, or errors. pc is preserved.
	bool runThreadStep(const ScriptProgram &program, uint32 startIndex,
	                   uint32 &pc, uint32 endIndex, bool &finished,
	                   uint32 maxSteps = 256);

	bool getVariable(const Common::String &name, int32 &value) const;
	bool setVariable(const Common::String &reference, int32 value);
	bool resolveValue(const Common::String &token, int32 &value) const;

	// Persistent snapshot used by the engine save/load layer. This serializes
	// script scalar variables, arrays and the RNG seed, but not a currently
	// executing program counter; foreground script calls are synchronous.
	void synchronize(Common::Serializer &s);

private:
	typedef Common::HashMap<Common::String, int32, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> VariableMap;
	typedef Common::HashMap<Common::String, Common::Array<int32>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> ArrayMap;

	bool declareVariable(const ScriptInstruction &instruction);
	bool declareArray(const ScriptInstruction &instruction);
	bool executeMove(const ScriptInstruction &instruction);
	bool executeArithmetic(const ScriptInstruction &instruction);
	bool executeRandom(const ScriptInstruction &instruction);
	bool evaluateComparison(const ScriptInstruction &instruction, bool &result) const;

	uint32 skipFalseBranch(const ScriptProgram &program, uint32 pc, uint32 endIndex) const;
	uint32 skipElseBranch(const ScriptProgram &program, uint32 pc, uint32 endIndex) const;
	bool runInternal(const ScriptProgram &program, uint32 rangeStart,
	                 uint32 &pc, uint32 endIndex, uint32 maxSteps,
	                 bool stopAtSchedulerBoundary, bool &finished);
	int labelIndexInRange(const ScriptProgram &program, const Common::String &name,
	                     uint32 startIndex, uint32 endIndex) const;
	bool threadBlockHasSchedulerJump(const ScriptProgram &program,
	                                 uint32 startIndex, uint32 endIndex) const;
	uint32 matchingThreadEnd(const ScriptProgram &program, uint32 pc,
	                         uint32 endIndex) const;

	bool splitReference(const Common::String &reference, Common::String &name,
	                    Common::String &indexExpression) const;
	bool parseInteger(const Common::String &token, int32 &value) const;

	ScriptVMHost *_host;
	Common::RandomSource _random;
	VariableMap _variables;
	ArrayMap _arrays;
};

} // namespace ZeroComico

#endif
