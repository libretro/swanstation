#pragma once
#include "gpu_types.h"
#include <memory>

class GPUBackend
{
public:
  GPUBackend();
  virtual ~GPUBackend();

  ALWAYS_INLINE uint16_t* GetVRAM() const { return m_vram_ptr; }

  virtual bool Initialize(bool force_thread);
  virtual void UpdateSettings();
  virtual void Reset(bool clear_vram);
  virtual void Shutdown();

  GPUBackendFillVRAMCommand* NewFillVRAMCommand();
  GPUBackendUpdateVRAMCommand* NewUpdateVRAMCommand(uint32_t num_words);
  GPUBackendCopyVRAMCommand* NewCopyVRAMCommand();
  GPUBackendSetDrawingAreaCommand* NewSetDrawingAreaCommand();
  GPUBackendDrawPolygonCommand* NewDrawPolygonCommand(uint32_t num_vertices);
  GPUBackendDrawRectangleCommand* NewDrawRectangleCommand();
  GPUBackendDrawLineCommand* NewDrawLineCommand(uint32_t num_vertices);

  void PushCommand(GPUBackendCommand* cmd);
  void Sync(bool allow_sleep);

protected:
  virtual void FillVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color, GPUBackendCommandParameters params) = 0;
  virtual void UpdateVRAM(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data,
                          GPUBackendCommandParameters params) = 0;
  virtual void CopyVRAM(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t width, uint32_t height,
                        GPUBackendCommandParameters params) = 0;
  virtual void DrawPolygon(const GPUBackendDrawPolygonCommand* cmd) = 0;
  virtual void DrawRectangle(const GPUBackendDrawRectangleCommand* cmd) = 0;
  virtual void DrawLine(const GPUBackendDrawLineCommand* cmd) = 0;
  virtual void FlushRender() = 0;

  void HandleCommand(const GPUBackendCommand* cmd);

  uint16_t* m_vram_ptr = nullptr;

  Common::Rectangle<uint32_t> m_drawing_area{};

  bool m_use_gpu_thread = false;

  static constexpr uint32_t COMMAND_QUEUE_SIZE = 4 * 1024 * 1024, THRESHOLD_TO_WAKE_GPU = 256;

private:
  struct ThreadState;

  static void GPUThreadEntryPoint(void* userdata);
  void RunGPULoop();

  void* AllocateCommand(GPUBackendCommandType command, uint32_t size);
  void WaitForSpace(size_t bytes);
  void WakeGPUThread();
  void StartGPUThread();
  void StopGPUThread();

  std::unique_ptr<ThreadState> m_thread;
};
