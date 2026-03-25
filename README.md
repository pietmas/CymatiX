# CymatiX

Real-time cymatics visualizer — transforms audio into GPU-rendered wave patterns using Vulkan.

![demo](media/cymatix.gif)

---

## Visual styles

- **Wave Interference** — ripple patterns driven by bass, mid, and treble bands
- **Lissajous** — frequency-ratio curves traced in real time from the audio spectrum

---

## Dependencies

```bash
sudo apt install cmake vulkan-tools libvulkan-dev glfw3-dev
```

Also requires a GPU with Vulkan support and up-to-date drivers.

---

## Build & run

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./cymatics
```

---

## Vulkan

The rendering backend is built on raw Vulkan following the [Vulkan Tutorial](https://docs.vulkan.org/tutorial/latest/00_Introduction.html).

---

## License

MIT
