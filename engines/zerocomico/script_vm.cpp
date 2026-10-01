/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: core script virtual machine
 */

#include "zerocomico/script_vm.h"
#include "zerocomico/script_program.h"

#include <cstdlib>

namespace ZeroComico {

namespace {

static bool isConditionalOpcode(const Common::String &op) {
	return op.equalsIgnoreCase("if_e") || op.equalsIgnoreCase("if_ne") ||
	       op.equalsIgnoreCase("if_g") || op.equalsIgnoreCase("if_ge") ||
	       op.equalsIgnoreCase("if_l") || op.equalsIgnoreCase("if_le") ||
	       op.equalsIgnoreCase("if_z") || op.equalsIgnoreCase("if_nz") ||
	       op.equalsIgnoreCase("ifobjselected") ||
	       op.equalsIgnoreCase("ifobjininv") ||
	       op.equalsIgnoreCase("ifcobjstate") ||
	       op.equalsIgnoreCase("ifcplace") ||
	       op.equalsIgnoreCase("ifplace") ||
	       op.equalsIgnoreCase("if_Char_InDialog") ||
	       op.equalsIgnoreCase("if_Is_OpenInterface") ||
	       op.equalsIgnoreCase("ifallobjnoselected") ||
	       op.equalsIgnoreCase("ifcombine") ||
	       op.equalsIgnoreCase("if_is_playingcut") ||
	       op.equalsIgnoreCase("if_is_openmenuinterface") ||
	       op.equalsIgnoreCase("if_key") ||
	       op.equalsIgnoreCase("if_No_Key_Pressed");
}

} // namespace

ScriptVM::ScriptVM(ScriptVMHost *host) : _host(host), _random("zerocomico-script-vm") {
	reset();
}

void ScriptVM::reset() {
	_variables.clear();
	_arrays.clear();

	// Zero Comico.exe registers these two engine-global arrays before loading
	// any gameplay script. The registration call receives 10 elements for
	// ge_Array and 5 for ge_toggle; both start zero-filled.
	Common::Array<int32> geArray;
	geArray.resize(10);
	for (uint32 i = 0; i < geArray.size(); ++i)
		geArray[i] = 0;
	_arrays["ge_Array"] = geArray;

	Common::Array<int32> geToggle;
	geToggle.resize(5);
	for (uint32 i = 0; i < geToggle.size(); ++i)
		geToggle[i] = 0;
	_arrays["ge_toggle"] = geToggle;
}

void ScriptVM::synchronize(Common::Serializer &s) {
	uint32 seed = _random.getSeed();
	s.syncAsUint32LE(seed);
	if (s.isLoading())
		_random.setSeed(seed);

	uint32 variableCount = s.isSaving() ? (uint32)_variables.size() : 0;
	s.syncAsUint32LE(variableCount);
	if (s.isLoading())
		_variables.clear();

	if (s.isSaving()) {
		for (VariableMap::const_iterator it = _variables.begin(); it != _variables.end(); ++it) {
			Common::String name = it->_key;
			int32 value = it->_value;
			s.syncString(name);
			s.syncAsSint32LE(value);
		}
	} else {
		for (uint32 i = 0; i < variableCount; ++i) {
			Common::String name;
			int32 value = 0;
			s.syncString(name);
			s.syncAsSint32LE(value);
			_variables[name] = value;
		}
	}

	uint32 arrayCount = s.isSaving() ? (uint32)_arrays.size() : 0;
	s.syncAsUint32LE(arrayCount);
	if (s.isLoading())
		_arrays.clear();

	if (s.isSaving()) {
		for (ArrayMap::const_iterator it = _arrays.begin(); it != _arrays.end(); ++it) {
			Common::String name = it->_key;
			s.syncString(name);
			uint32 valueCount = (uint32)it->_value.size();
			s.syncAsUint32LE(valueCount);
			for (uint32 i = 0; i < valueCount; ++i) {
				int32 value = it->_value[i];
				s.syncAsSint32LE(value);
			}
		}
	} else {
		for (uint32 arrayIndex = 0; arrayIndex < arrayCount; ++arrayIndex) {
			Common::String name;
			s.syncString(name);
			uint32 valueCount = 0;
			s.syncAsUint32LE(valueCount);
			Common::Array<int32> values;
			values.resize(valueCount);
			for (uint32 i = 0; i < valueCount; ++i)
				s.syncAsSint32LE(values[i]);
			_arrays[name] = values;
		}
	}
}

bool ScriptVM::parseInteger(const Common::String &token, int32 &value) const {
	if (token.empty())
		return false;

	char *end = nullptr;
	const long parsed = strtol(token.c_str(), &end, 0);
	if (end == token.c_str() || *end != 0)
		return false;

	value = (int32)parsed;
	return true;
}

bool ScriptVM::splitReference(const Common::String &reference, Common::String &name,
                              Common::String &indexExpression) const {
	const uint32 open = reference.find('(');
	if (open == Common::String::npos || reference.size() < open + 3 ||
	    reference[reference.size() - 1] != ')')
		return false;

	name = reference.substr(0, open);
	indexExpression = reference.substr(open + 1, reference.size() - open - 2);
	return !name.empty() && !indexExpression.empty();
}

bool ScriptVM::getVariable(const Common::String &name, int32 &value) const {
	VariableMap::const_iterator it = _variables.find(name);
	if (it == _variables.end())
		return false;
	value = it->_value;
	return true;
}

bool ScriptVM::resolveValue(const Common::String &token, int32 &value) const {
	Common::String arrayName;
	Common::String indexExpression;
	if (splitReference(token, arrayName, indexExpression)) {
		int32 index = 0;
		if (!resolveValue(indexExpression, index) || index < 0)
			return false;

		ArrayMap::const_iterator it = _arrays.find(arrayName);
		if (it == _arrays.end() || (uint32)index >= it->_value.size())
			return false;
		value = it->_value[(uint32)index];
		return true;
	}

	if (getVariable(token, value))
		return true;

	return parseInteger(token, value);
}

bool ScriptVM::setVariable(const Common::String &reference, int32 value) {
	Common::String arrayName;
	Common::String indexExpression;
	if (splitReference(reference, arrayName, indexExpression)) {
		int32 index = 0;
		if (!resolveValue(indexExpression, index) || index < 0)
			return false;

		ArrayMap::iterator it = _arrays.find(arrayName);
		if (it == _arrays.end() || (uint32)index >= it->_value.size())
			return false;
		it->_value[(uint32)index] = value;
		return true;
	}

	_variables[reference] = value;
	return true;
}

bool ScriptVM::declareVariable(const ScriptInstruction &instruction) {
	if (instruction.args.empty())
		return false;

	int32 value = 0;
	if (instruction.args.size() >= 2 && !resolveValue(instruction.args[1], value))
		return false;

	_variables[instruction.args[0]] = value;
	return true;
}

bool ScriptVM::declareArray(const ScriptInstruction &instruction) {
	if (instruction.args.empty())
		return false;

	Common::Array<int32> values;
	for (uint32 i = 1; i < instruction.args.size(); ++i) {
		int32 value = 0;
		if (!resolveValue(instruction.args[i], value))
			return false;
		values.push_back(value);
	}

	_arrays[instruction.args[0]] = values;
	return true;
}

bool ScriptVM::executeMove(const ScriptInstruction &instruction) {
	if (instruction.args.size() < 2)
		return false;

	int32 value = 0;
	if (!resolveValue(instruction.args[1], value))
		return false;
	return setVariable(instruction.args[0], value);
}

bool ScriptVM::executeArithmetic(const ScriptInstruction &instruction) {
	if (instruction.args.empty())
		return false;

	int32 current = 0;
	if (!resolveValue(instruction.args[0], current))
		return false;

	if (instruction.opcode.equalsIgnoreCase("inc"))
		return setVariable(instruction.args[0], current + 1);
	if (instruction.opcode.equalsIgnoreCase("dec"))
		return setVariable(instruction.args[0], current - 1);

	if (instruction.args.size() < 2)
		return false;

	int32 operand = 0;
	if (!resolveValue(instruction.args[1], operand))
		return false;

	if (instruction.opcode.equalsIgnoreCase("add"))
		return setVariable(instruction.args[0], current + operand);
	if (instruction.opcode.equalsIgnoreCase("sub"))
		return setVariable(instruction.args[0], current - operand);
	if (instruction.opcode.equalsIgnoreCase("mul"))
		return setVariable(instruction.args[0], current * operand);
	if (instruction.opcode.equalsIgnoreCase("div")) {
		if (operand == 0)
			return false;
		return setVariable(instruction.args[0], current / operand);
	}

	return false;
}


bool ScriptVM::executeRandom(const ScriptInstruction &instruction) {
	if (instruction.args.size() < 2)
		return false;

	int32 upperBound = 0;
	if (!resolveValue(instruction.args[1], upperBound) || upperBound <= 0)
		return false;

	// Retail scripts use Rnd x 2 followed by tests for 0/1, so the second
	// operand is an exclusive upper bound rather than Common::RandomSource's
	// inclusive max.
	const int32 value = (int32)_random.getRandomNumber((uint)upperBound - 1U);
	return setVariable(instruction.args[0], value);
}

bool ScriptVM::evaluateComparison(const ScriptInstruction &instruction, bool &result) const {
	if (instruction.opcode.equalsIgnoreCase("ifobjselected") ||
	    instruction.opcode.equalsIgnoreCase("ifobjininv") ||
	    instruction.opcode.equalsIgnoreCase("ifcobjstate") ||
	    instruction.opcode.equalsIgnoreCase("ifcplace") ||
	    instruction.opcode.equalsIgnoreCase("ifplace") ||
	    instruction.opcode.equalsIgnoreCase("if_Char_InDialog") ||
	    instruction.opcode.equalsIgnoreCase("if_Is_OpenInterface") ||
	    instruction.opcode.equalsIgnoreCase("ifallobjnoselected") ||
	    instruction.opcode.equalsIgnoreCase("ifcombine") ||
	    instruction.opcode.equalsIgnoreCase("if_is_playingcut") ||
	    instruction.opcode.equalsIgnoreCase("if_is_openmenuinterface") ||
	    instruction.opcode.equalsIgnoreCase("if_key") ||
	    instruction.opcode.equalsIgnoreCase("if_No_Key_Pressed"))
		return _host && _host->evaluateScriptCondition(instruction, result);

	if (instruction.args.empty())
		return false;

	int32 left = 0;
	if (!resolveValue(instruction.args[0], left))
		return false;

	if (instruction.opcode.equalsIgnoreCase("if_z")) {
		result = left == 0;
		return true;
	}
	if (instruction.opcode.equalsIgnoreCase("if_nz")) {
		result = left != 0;
		return true;
	}

	if (instruction.args.size() < 2)
		return false;
	int32 right = 0;
	if (!resolveValue(instruction.args[1], right))
		return false;

	if (instruction.opcode.equalsIgnoreCase("if_e"))
		result = left == right;
	else if (instruction.opcode.equalsIgnoreCase("if_ne"))
		result = left != right;
	else if (instruction.opcode.equalsIgnoreCase("if_g"))
		result = left > right;
	else if (instruction.opcode.equalsIgnoreCase("if_ge"))
		result = left >= right;
	else if (instruction.opcode.equalsIgnoreCase("if_l"))
		result = left < right;
	else if (instruction.opcode.equalsIgnoreCase("if_le"))
		result = left <= right;
	else
		return false;

	return true;
}

uint32 ScriptVM::skipFalseBranch(const ScriptProgram &program, uint32 pc, uint32 endIndex) const {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	uint32 nested = 0;

	for (uint32 i = pc + 1; i < endIndex; ++i) {
		const Common::String &op = instructions[i].opcode;
		if (isConditionalOpcode(op)) {
			++nested;
		} else if (op.equalsIgnoreCase("endif")) {
			if (nested == 0)
				return i + 1;
			--nested;
		} else if (op.equalsIgnoreCase("else") && nested == 0) {
			return i + 1;
		}
	}

	return endIndex;
}

uint32 ScriptVM::skipElseBranch(const ScriptProgram &program, uint32 pc, uint32 endIndex) const {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	uint32 nested = 0;

	for (uint32 i = pc + 1; i < endIndex; ++i) {
		const Common::String &op = instructions[i].opcode;
		if (isConditionalOpcode(op)) {
			++nested;
		} else if (op.equalsIgnoreCase("endif")) {
			if (nested == 0)
				return i + 1;
			--nested;
		}
	}

	return endIndex;
}

uint32 ScriptVM::matchingThreadEnd(const ScriptProgram &program, uint32 pc,
                                      uint32 endIndex) const {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	uint32 nestedThreads = 0;
	for (uint32 next = pc + 1; next < endIndex; ++next) {
		const Common::String &threadOp = instructions[next].opcode;
		if (threadOp.equalsIgnoreCase("begin_thread") ||
		    threadOp.equalsIgnoreCase("begin_rthread")) {
			++nestedThreads;
		} else if (threadOp.equalsIgnoreCase("end_thread")) {
			if (nestedThreads == 0)
				return next;
			--nestedThreads;
		}
	}
	return endIndex;
}

bool ScriptVM::threadBlockHasSchedulerJump(const ScriptProgram &program,
                                            uint32 startIndex, uint32 endIndex) const {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	uint32 nestedThreads = 0;
	for (uint32 i = startIndex; i < endIndex; ++i) {
		const Common::String &op = instructions[i].opcode;
		if (op.equalsIgnoreCase("begin_thread") ||
		    op.equalsIgnoreCase("begin_rthread")) {
			++nestedThreads;
			continue;
		}
		if (op.equalsIgnoreCase("end_thread")) {
			if (nestedThreads > 0)
				--nestedThreads;
			continue;
		}
		if (nestedThreads != 0)
			continue;
		if (op.equalsIgnoreCase("wjmp") ||
		    op.equalsIgnoreCase("wjmp_if_z") ||
		    op.equalsIgnoreCase("wjmp_if_nz"))
			return true;
	}
	return false;
}

int ScriptVM::labelIndexInRange(const ScriptProgram &program, const Common::String &name,
                                uint32 startIndex, uint32 endIndex) const {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	if (endIndex > instructions.size())
		endIndex = instructions.size();
	for (uint32 i = startIndex; i < endIndex; ++i) {
		if (instructions[i].opcode.equalsIgnoreCase("label") &&
		    !instructions[i].args.empty() &&
		    instructions[i].args[0].equalsIgnoreCase(name))
			return (int)i;
	}
	return -1;
}

bool ScriptVM::runInternal(const ScriptProgram &program, uint32 rangeStart,
                           uint32 &pc, uint32 endIndex, uint32 maxSteps,
                           bool stopAtSchedulerBoundary, bool &finished) {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	if (pc > instructions.size())
		return false;
	if (endIndex > instructions.size())
		endIndex = instructions.size();

	finished = false;
	uint32 steps = 0;

	while (pc < endIndex) {
		if (++steps > maxSteps)
			return false;

		const ScriptInstruction &instruction = instructions[pc];
		const Common::String &op = instruction.opcode;

		if (op.equalsIgnoreCase("Variable")) {
			if (!declareVariable(instruction))
				return false;
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("array")) {
			if (!declareArray(instruction))
				return false;
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("mov")) {
			if (!executeMove(instruction))
				return false;
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("inc") || op.equalsIgnoreCase("dec") ||
		    op.equalsIgnoreCase("add") || op.equalsIgnoreCase("sub") ||
		    op.equalsIgnoreCase("mul") || op.equalsIgnoreCase("div")) {
			if (!executeArithmetic(instruction))
				return false;
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("Rnd")) {
			if (!executeRandom(instruction))
				return false;
			++pc;
			continue;
		}

		if (isConditionalOpcode(op)) {
			bool condition = false;
			if (!evaluateComparison(instruction, condition))
				return false;
			pc = condition ? pc + 1 : skipFalseBranch(program, pc, endIndex);
			continue;
		}

		if (op.equalsIgnoreCase("else")) {
			pc = skipElseBranch(program, pc, endIndex);
			continue;
		}

		if (op.equalsIgnoreCase("endif") || op.equalsIgnoreCase("label") ||
		    op.equalsIgnoreCase("end_thread")) {
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("begin_thread")) {
			const uint32 blockEnd = matchingThreadEnd(program, pc, endIndex);
			if (blockEnd >= endIndex) {
				++pc;
				continue;
			}
			if (threadBlockHasSchedulerJump(program, pc + 1, blockEnd)) {
				if (!_host || !_host->scheduleScriptThread(program, pc + 1, blockEnd))
					return false;
				pc = blockEnd + 1;
				continue;
			}
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("jmp") || op.equalsIgnoreCase("wjmp")) {
			if (instruction.args.empty())
				return false;
			const int target = labelIndexInRange(program, instruction.args[0], rangeStart, endIndex);
			if (target < 0 || (uint32)target >= endIndex)
				return false;
			if (op.equalsIgnoreCase("wjmp")) {
				if (stopAtSchedulerBoundary) {
					pc = (uint32)target + 1;
					return true;
				}
				if (!_host || !_host->yieldScriptExecution())
					return false;
			}
			pc = (uint32)target + 1;
			continue;
		}

		if (op.equalsIgnoreCase("jmp_if_key")) {
			if (instruction.args.size() < 2 || !_host)
				return false;
			bool pressed = false;
			if (!_host->evaluateScriptCondition(instruction, pressed))
				return false;
			if (pressed) {
				const int target = labelIndexInRange(program, instruction.args[1], rangeStart, endIndex);
				if (target < 0 || (uint32)target >= endIndex)
					return false;
				pc = (uint32)target + 1;
			} else {
				++pc;
			}
			continue;
		}

		if (op.equalsIgnoreCase("jmp_if_l") || op.equalsIgnoreCase("jmp_if_le") ||
		    op.equalsIgnoreCase("jmp_if_g") || op.equalsIgnoreCase("jmp_if_ge") ||
		    op.equalsIgnoreCase("jmp_if_e") || op.equalsIgnoreCase("jmp_if_ne")) {
			if (instruction.args.size() < 3)
				return false;
			int32 left = 0;
			int32 right = 0;
			if (!resolveValue(instruction.args[0], left) ||
			    !resolveValue(instruction.args[1], right))
				return false;

			bool jump = false;
			if (op.equalsIgnoreCase("jmp_if_l"))
				jump = left < right;
			else if (op.equalsIgnoreCase("jmp_if_le"))
				jump = left <= right;
			else if (op.equalsIgnoreCase("jmp_if_g"))
				jump = left > right;
			else if (op.equalsIgnoreCase("jmp_if_ge"))
				jump = left >= right;
			else if (op.equalsIgnoreCase("jmp_if_e"))
				jump = left == right;
			else
				jump = left != right;

			if (jump) {
				const int target = labelIndexInRange(program, instruction.args[2], rangeStart, endIndex);
				if (target < 0 || (uint32)target >= endIndex)
					return false;
				pc = (uint32)target + 1;
			} else {
				++pc;
			}
			continue;
		}

		if (op.equalsIgnoreCase("jmp_if_z") || op.equalsIgnoreCase("jmp_if_nz") ||
		    op.equalsIgnoreCase("wjmp_if_z") || op.equalsIgnoreCase("wjmp_if_nz")) {
			if (instruction.args.size() < 2)
				return false;
			int32 value = 0;
			if (!resolveValue(instruction.args[0], value))
				return false;

			const bool isWaitJump =
				op.equalsIgnoreCase("wjmp_if_z") || op.equalsIgnoreCase("wjmp_if_nz");
			const bool testZero =
				op.equalsIgnoreCase("jmp_if_z") || op.equalsIgnoreCase("wjmp_if_z");
			const bool jump = testZero ? value == 0 : value != 0;
			if (jump) {
				const int target = labelIndexInRange(program, instruction.args[1], rangeStart, endIndex);
				if (target < 0 || (uint32)target >= endIndex)
					return false;
				if (isWaitJump) {
					if (stopAtSchedulerBoundary) {
						pc = (uint32)target + 1;
						return true;
					}
					if (!_host || !_host->yieldScriptExecution())
						return false;
				}
				pc = (uint32)target + 1;
			} else {
				++pc;
			}
			continue;
		}

		// Real-time thread blocks are editor/debug background loops. They remain
		// skipped in the foreground VM; unlike begin_thread they are not needed
		// by shipped progression logic.
		if (op.equalsIgnoreCase("begin_rthread")) {
			const uint32 blockEnd = matchingThreadEnd(program, pc, endIndex);
			pc = blockEnd < endIndex ? blockEnd + 1 : endIndex;
			continue;
		}

		if (!_host || !_host->executeScriptOpcode(instruction))
			return false;

		++pc;
	}

	finished = true;
	return true;
}

bool ScriptVM::run(const ScriptProgram &program, uint32 startIndex, uint32 endIndex, uint32 maxSteps) {
	uint32 pc = startIndex;
	bool finished = false;
	return runInternal(program, startIndex, pc, endIndex, maxSteps, false, finished) && finished;
}

bool ScriptVM::runThreadStep(const ScriptProgram &program, uint32 startIndex,
                             uint32 &pc, uint32 endIndex, bool &finished,
                             uint32 maxSteps) {
	return runInternal(program, startIndex, pc, endIndex, maxSteps, true, finished);
}

} // namespace ZeroComico
