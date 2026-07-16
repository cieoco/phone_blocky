#ifndef COMM_CHANNEL_H
#define COMM_CHANNEL_H

#include <stdint.h>

// 通訊通道位元遮罩（通訊層解耦的核心）。
//
// 用意：把「傳輸（WS / Serial）」與「協定（JSON router）」「回應出口（sink）」
// 解耦。CommandProcessor 不再認識任何實體傳輸，只用 Comm::Channel 位元遮罩
// 標記「指令從哪來、回應/遙測該往哪去」；唯一知道實體線路怎麼寫的是 sink
// （WebServerHandler::sendJsonResponse）。
// 成員加 CH_ 前綴：避開 Arduino.h 的 SERIAL 巨集（巨集只比對完整 token，
// CH_SERIAL 不會被替換）。
namespace Comm {
enum Channel : uint8_t {
  CH_NONE = 0,
  CH_WS = 1 << 0,     // WebSocket（ws://<ip>/ws）
  CH_SERIAL = 1 << 1, // USB 序列埠
  CH_ALL = CH_WS | CH_SERIAL,
};
}

#endif
