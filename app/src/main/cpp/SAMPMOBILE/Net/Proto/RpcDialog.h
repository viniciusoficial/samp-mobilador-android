#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>
#include <string>

namespace proto {

// RPC 61 (server -> client): ShowPlayerDialog / HidePlayerDialog (id < 0)
struct ShowDialog {
	static constexpr int kRpcId = rpc::kDialogBox;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ShowDialog";

	int16_t id = 0;
	uint8_t style = 0;
	std::string title;
	std::string button1;
	std::string button2;
	std::string body;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i16(id) && s.u8(style) && s.dynStr8(title) && s.dynStr8(button1) && s.dynStr8(button2) &&
			s.compressedStr(body, 4096);
	}
};

// RPC 62 (client -> server): OnDialogResponse
struct DialogResponse {
	static constexpr int kRpcId = rpc::kDialogResponse;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DialogResponse";

	int16_t id = 0;
	uint8_t response = 0;
	int16_t listItem = -1;
	std::string input;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i16(id) && s.u8(response) && s.i16(listItem) && s.dynStr8(input);
	}
};

} // namespace proto
