/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JACS sequence state tables
 */

#include "zerocomico/sequence_script.h"
#include "zerocomico/script_program.h"

namespace ZeroComico {

bool SequenceScript::load(const Common::Path &path) {
	ScriptProgram program;
	if (!program.load(path))
		return false;
	return parse(program);
}

bool SequenceScript::parse(const ScriptProgram &program) {
	bodyName.clear();
	sequences.clear();

	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &inst = instructions[i];

		if (inst.opcode.equalsIgnoreCase("Body") && !inst.args.empty()) {
			bodyName = inst.args[0];
			continue;
		}

		if (!inst.opcode.equalsIgnoreCase("Seq") || inst.args.empty())
			continue;

		AnimationSequence sequence;
		sequence.name = inst.args[0];
		const int sequenceDepth = inst.depth;

		for (uint32 j = i + 1; j < instructions.size(); ++j) {
			const ScriptInstruction &child = instructions[j];
			if (child.depth <= sequenceDepth)
				break;

			const bool transition =
				child.opcode == "0>1" || child.opcode == "1>1" ||
				child.opcode == "1>0" || child.opcode == "0>0";
			if (transition) {
				SequenceTransition value;
				value.state = child.opcode;
				value.clips = child.args;
				sequence.transitions.push_back(value);
				continue;
			}

			if (child.opcode.equalsIgnoreCase("end_seq")) {
				const int blockDepth = child.depth;
				for (uint32 k = j + 1; k < instructions.size(); ++k) {
					const ScriptInstruction &row = instructions[k];
					if (row.depth <= blockDepth)
						break;
					sequence.endClips.push_back(row.opcode);
					for (uint32 a = 0; a < row.args.size(); ++a)
						sequence.endClips.push_back(row.args[a]);
				}
				continue;
			}

			if (child.opcode.equalsIgnoreCase("table")) {
				const int blockDepth = child.depth;
				for (uint32 k = j + 1; k < instructions.size(); ++k) {
					const ScriptInstruction &row = instructions[k];
					if (row.depth <= blockDepth)
						break;
					SequenceTableRow tableRow;
					tableRow.clips.push_back(row.opcode);
					for (uint32 a = 0; a < row.args.size(); ++a)
						tableRow.clips.push_back(row.args[a]);
					sequence.table.push_back(tableRow);
				}
			}
		}

		if (!sequence.transitions.empty())
			sequences.push_back(sequence);
	}

	return !bodyName.empty() && !sequences.empty();
}

const AnimationSequence *SequenceScript::findSequence(const Common::String &name) const {
	for (uint32 i = 0; i < sequences.size(); ++i)
		if (sequences[i].name.equalsIgnoreCase(name))
			return &sequences[i];
	return nullptr;
}

} // namespace ZeroComico
