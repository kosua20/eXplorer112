#include "Window.hpp"

#include "input/InputCallbacks.hpp"
#include "input/Input.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"
#include "graphics/Swapchain.hpp"
#include "core/System.hpp"
#include "core/Log.hpp"

#include "Common.hpp"


#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>

#include <sr_gui/sr_gui.h>

#include <cstring>

Window::Window(const std::string & name, RenderingConfig & config, bool escapeQuit, bool hidden) :
_config(config), _allowEscape(escapeQuit) {
	// Initialize glfw.
	if(!glfwInit()) {
		Log::error("GPU: Could not start GLFW3.");
		return;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUSED, hidden ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_AUTO_ICONIFY, GL_FALSE); // Avoid random minification in fullscreen on macOS.
	const GLFWvidmode * mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

	if(config.fullscreen) {
		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
		/// \note We might want to impose the configured size here. This means the monitor could be set in a non-native mode.
		_window = glfwCreateWindow(mode->width, mode->height, name.c_str(), glfwGetPrimaryMonitor(), nullptr);
	} else {
		// Create a window with a given size. Width and height are defined in the configuration.
		int width = _config.initialWidth != 0 ? int(_config.initialWidth) : mode->width;
		int height = _config.initialHeight != 0 ? int(_config.initialHeight) : mode->height;
		_window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
	}

	if(!_window) {
		Log::error("GPU: Could not open window with GLFW3");
		glfwTerminate();
		return;
	}

	if(_config.forceAspectRatio) {
		glfwSetWindowAspectRatio(_window, int(_config.initialWidth), int(_config.initialHeight));
	}

	// Setup callbacks for various interactions and inputs.
	glfwSetFramebufferSizeCallback(_window, resize_callback);		  // Resizing the window
	glfwSetKeyCallback(_window, key_callback);						  // Pressing a key
	glfwSetCharCallback(_window, char_callback);						  // Outputing a text char (for ImGui)
	glfwSetMouseButtonCallback(_window, mouse_button_callback);		  // Clicking the mouse buttons
	glfwSetCursorPosCallback(_window, cursor_pos_callback);			  // Moving the cursor
	glfwSetScrollCallback(_window, scroll_callback);					  // Scrolling
	glfwSetJoystickCallback(joystick_callback);						  // Joystick
	glfwSetWindowIconifyCallback(_window, iconify_callback);			  // Window minimization
	glfwSwapInterval(_config.vsync ? (_config.rate == 30 ? 2 : 1) : 0); // 60 FPS V-sync

	glfwSetWindowFocusCallback(_window, window_focus_callback);
	glfwSetCursorEnterCallback(_window, cursor_enter_callback);
	glfwSetMonitorCallback(monitor_callback);

	// Check the window position and size (if we are on a screen smaller than the initial size).
	glfwGetWindowPos(_window, &_config.windowFrame[0], &_config.windowFrame[1]);
	glfwGetWindowSize(_window, &_config.windowFrame[2], &_config.windowFrame[3]);

	// On HiDPI screens, we have to consider the internal resolution for all framebuffers size.
	int width, height;
	glfwGetFramebufferSize(_window, &width, &height);
	config.screenResolution = glm::vec2(width, height);

	// Compute point density by computing the ratio.
	const float screenDensity = float(width) / float(_config.windowFrame[2]);
	Input::manager().densityEvent(screenDensity);

	// Setup the GPU.
	if(!GPU::setup(name)){
		glfwDestroyWindow(_window);
		glfwTerminate();
		return;
	}

	sr_gui_init();

	// Create a swapchain associated to the window.
	GPU::setupWindow(this);

	setupImGui();

	// Update the resolution.
	Input::manager().resizeEvent(width, height);
}

void Window::perform(Action action) {
	switch(action) {
		case Action::Quit:
			glfwSetWindowShouldClose(_window, GLFW_TRUE);
			break;
		case Action::Vsync: {
			if(_config.vsync) {
				glfwSwapInterval(0);
			} else {
				glfwSwapInterval(_config.rate == 30 ? 2 : 1);
			}
			_config.vsync = !_config.vsync;
			break;
		}
		case Action::Fullscreen: {
			// Are we currently fullscreen?
			const bool fullscreen = glfwGetWindowMonitor(_window) != nullptr;

			if(fullscreen) {
				// Restore the window position and size.
				glfwSetWindowMonitor(_window, nullptr, _config.windowFrame[0], _config.windowFrame[1], _config.windowFrame[2], _config.windowFrame[3], 0);
				// Check the window position and size (if we are on a screen smaller than the initial size).
				glfwGetWindowPos(_window, &_config.windowFrame[0], &_config.windowFrame[1]);
				glfwGetWindowSize(_window, &_config.windowFrame[2], &_config.windowFrame[3]);
			} else {
				// Backup the window current frame.
				glfwGetWindowPos(_window, &_config.windowFrame[0], &_config.windowFrame[1]);
				glfwGetWindowSize(_window, &_config.windowFrame[2], &_config.windowFrame[3]);
				// Move to fullscreen on the primary monitor.
				GLFWmonitor * monitor	= glfwGetPrimaryMonitor();
				const GLFWvidmode * mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
			}
			_config.fullscreen = !fullscreen;
			// On some hardware, V-sync options can be lost.
			glfwSwapInterval(_config.vsync ? (_config.rate == 30 ? 2 : 1) : 0);

			// On HiDPI screens, we have to consider the internal resolution for all framebuffers size.
			int width, height;
			glfwGetFramebufferSize(_window, &width, &height);
			_config.screenResolution = glm::vec2(width, height);

			int wwidth, hheight;
			glfwGetWindowSize(_window, &wwidth, &hheight);
			// Compute point density by computing the ratio.
			const float screenDensity = float(width) / float(wwidth);
			Input::manager().densityEvent(screenDensity);

			// Update the resolution.
			Input::manager().resizeEvent(width, height);
		} break;
		default:
			break;
	}
}

void Window::bind(const LoadOperation& colorOp, const LoadOperation& depthOp, const LoadOperation& stencilOp){
	GPU::bind(_swapchain->color(), _swapchain->depth(), colorOp, depthOp, stencilOp);
}

Texture& Window::color(){
	return _swapchain->color();
}

bool Window::nextFrame() {
	if(_frameStarted){
		// Render the interface.
		ImGui::Render();

		// Draw ImGui.
		GPU::pushMarker( "GUI" );
		bind(LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::LOAD);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), GPU::getInternal()->getRenderCommandBuffer());
		GPU::popMarker( );
	}

	// Notify GPU for book-keeping.
	bool validSwapchain = _swapchain->nextFrame();

	do {
		// Update events (inputs,...).
		Input::manager().update();
		// On some platforms, when the window is minimized, the swapchain is invalidated and expected to have a 0,0 extent
		// which is technicaly forbidden by the specification, leading to invalidate operations.
		// To prevent this, wait for new events without rendering while we are minimized.
		if(Input::manager().minimized()){
			continue;
		}
		// Handle quitting.
		if(_allowEscape && Input::manager().pressed(Input::Key::Escape)) {
			perform(Action::Quit);
		}
		// Check if the backbuffer was resized.
		if(Input::manager().resized() || !validSwapchain){
			uint w = uint(Input::manager().size()[0]);
			uint h = uint(Input::manager().size()[1]);

			// If minimized and undetected, waiting for events is not working, poll in a loop.
			while(w == 0 || h == 0){
				Input::manager().update();
				w = uint(Input::manager().size()[0]);
				h = uint(Input::manager().size()[1]);
			}

			_swapchain->resize(w, h);
			// We should probably jump to the next frame here.
			validSwapchain = _swapchain->nextFrame();
			ImGui_ImplVulkan_SetMinImageCount(_swapchain->minCount());
		}
	} while(!validSwapchain);

	// Start new GUI frame.
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	_frameStarted = true;
	const bool shouldClose = glfwWindowShouldClose(_window);
	return !shouldClose;
}

Window::~Window() {
	// Make sure rendering is done.
	if(_frameStarted){
		ImGui::EndFrame();
	}
	_swapchain.reset(nullptr);

	// Clean the interface.
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	// Close context and any other GLFW resources.
	GPU::cleanup();

	sr_gui_cleanup();

	glfwDestroyWindow(_window);
	glfwTerminate();
}

void Window::setupImGui() {
	ImGui::CreateContext();
	ImGuiIO & io = ImGui::GetIO();
	// General settings.
	io.ConfigFlags = ImGuiConfigFlags_DockingEnable;
	
	// Load font.
	size_t fontSize = 0;
	const char * fontData = System::loadData( APP_RESOURCE_DIRECTORY / "Lato-Regular.ttf", fontSize);
	if(fontData){
		ImFontConfig font = ImFontConfig();
		font.FontData = (void*)(fontData);
		font.FontDataSize = int(fontSize);
		font.SizePixels = 16.0f;
		font.FontDataOwnedByAtlas = true;
		io.Fonts->AddFont(&font);
	}
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	io.Fonts->TexID = 0;
	ImGui_ImplGlfw_InitForVulkan(_window, false);

	_imgui = new ImGui_ImplVulkan_InitInfo;
	std::memset(_imgui, 0, sizeof(ImGui_ImplVulkan_InitInfo));

	GPUContext* context = GPU::getInternal();
	_imgui->Instance = context->instance;
	_imgui->PhysicalDevice = context->physicalDevice;
	_imgui->Device = context->device;
	_imgui->QueueFamily = context->graphicsId;
	_imgui->Queue = context->graphicsQueue;
	_imgui->PipelineCache = VK_NULL_HANDLE;
	_imgui->DescriptorPool = context->descriptorAllocator.getImGuiPool();
	_imgui->Subpass = 0;
	_swapchain->getFormats(_imgui->ColorFormat, _imgui->DepthFormat, _imgui->StencilFormat);
	_imgui->MinImageCount = _swapchain->minCount();
	_imgui->ImageCount = _swapchain->count();
	_imgui->MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	_imgui->Allocator = nullptr;
	_imgui->CheckVkResultFn = &VkUtils::checkResult;

	ImGui_ImplVulkan_Init(_imgui, VK_NULL_HANDLE);

	// Upload font.
	VkCommandBuffer commandBuffer = VkUtils::beginSyncOperations(*context);
	ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
	VkUtils::endSyncOperations(commandBuffer, *context);
	ImGui_ImplVulkan_DestroyFontUploadObjects();
	
	// Customize the style.
	ImGui::StyleColorsDark();
	ImGuiStyle & style = ImGui::GetStyle();
	// Colors.
	const float gammaExp = 1.f/2.2f;
	const glm::vec4 gamma(gammaExp, gammaExp, gammaExp, 1.f);

	ImVec4 * colors						    = style.Colors;
	const ImVec4 bgColor					= glm::pow(glm::vec4(0.01f, 0.01f, 0.01f, 1.00f), gamma);
	const ImVec4 buttonColor				= glm::pow(glm::vec4(0.04f, 0.04f, 0.04f, 1.00f), gamma);
	const ImVec4 buttonHoverColor			= glm::pow(glm::vec4(0.10f, 0.10f, 0.10f, 1.00f), gamma);
	const ImVec4 buttonActiveColor			= glm::pow(glm::vec4(0.16f, 0.16f, 0.16f, 1.00f), gamma);
	const ImVec4 textColor					= glm::pow(glm::vec4(0.65f, 0.65f, 0.65f, 1.00f), gamma);

	colors[ImGuiCol_TextDisabled]           = glm::pow(glm::vec4(0.22f, 0.22f, 0.22f, 1.00f), gamma);
	colors[ImGuiCol_ChildBg]                = glm::pow(glm::vec4(0.00f, 0.00f, 0.00f, 0.00f), gamma);
	colors[ImGuiCol_PopupBg]                = glm::pow(glm::vec4(0.01f, 0.01f, 0.01f, 1.00f), gamma);
	colors[ImGuiCol_Border]                 = glm::pow(glm::vec4(0.00f, 0.00f, 0.00f, 0.50f), gamma);
	colors[ImGuiCol_BorderShadow]           = glm::pow(glm::vec4(0.00f, 0.00f, 0.00f, 0.00f), gamma);
	colors[ImGuiCol_FrameBg]                = glm::pow(glm::vec4(0.025f, 0.025f, 0.025f, 1.00f), gamma);
	colors[ImGuiCol_FrameBgHovered]         = glm::pow(glm::vec4(0.02f, 0.02f, 0.02f, 1.00f), gamma);
	colors[ImGuiCol_FrameBgActive]          = glm::pow(glm::vec4(0.05f, 0.05f, 0.05f, 1.00f), gamma);
	colors[ImGuiCol_TitleBgActive]          = glm::pow(glm::vec4(0.02f, 0.02f, 0.02f, 1.00f), gamma);
	colors[ImGuiCol_MenuBarBg]              = glm::pow(glm::vec4(0.02f, 0.02f, 0.02f, 1.00f), gamma);
	colors[ImGuiCol_ScrollbarBg]            = glm::pow(glm::vec4(0.00f, 0.00f, 0.00f, 0.00f), gamma);
	colors[ImGuiCol_TabActive]              = glm::pow(glm::vec4(0.14f, 0.14f, 0.14f, 1.00f), gamma);
	colors[ImGuiCol_TabUnfocused]           = glm::pow(glm::vec4(0.02f, 0.02f, 0.02f, 0.97f), gamma);
	colors[ImGuiCol_TabUnfocusedActive]     = glm::pow(glm::vec4(0.16f, 0.16f, 0.16f, 1.00f), gamma);
	colors[ImGuiCol_PlotLines]              = glm::pow(glm::vec4(0.34f, 0.34f, 0.34f, 1.00f), gamma);
	colors[ImGuiCol_PlotLinesHovered]       = glm::pow(glm::vec4(0.91f, 0.91f, 0.91f, 1.00f), gamma);
	colors[ImGuiCol_PlotHistogram]          = glm::pow(glm::vec4(0.35f, 0.35f, 0.35f, 1.00f), gamma);
	colors[ImGuiCol_PlotHistogramHovered]   = glm::pow(glm::vec4(0.79f, 0.79f, 0.79f, 1.00f), gamma);
	colors[ImGuiCol_TableHeaderBg]          = glm::pow(glm::vec4(0.08f, 0.08f, 0.08f, 1.00f), gamma);
	colors[ImGuiCol_TableBorderStrong]      = glm::pow(glm::vec4(0.08f, 0.08f, 0.08f, 1.00f), gamma);
	colors[ImGuiCol_TableBorderLight]       = glm::pow(glm::vec4(0.19f, 0.19f, 0.19f, 1.00f), gamma);
	colors[ImGuiCol_TableRowBg]             = glm::pow(glm::vec4(0.00f, 0.00f, 0.00f, 0.00f), gamma);
	colors[ImGuiCol_TableRowBgAlt]          = glm::pow(glm::vec4(0.04f, 0.04f, 0.04f, 1.00f), gamma);
	colors[ImGuiCol_NavHighlight]           = glm::pow(glm::vec4(0.75f, 0.75f, 0.75f, 1.00f), gamma);
	colors[ImGuiCol_NavWindowingHighlight]  = glm::pow(glm::vec4(1.00f, 1.00f, 1.00f, 0.70f), gamma);
	colors[ImGuiCol_NavWindowingDimBg]      = glm::pow(glm::vec4(0.61f, 0.61f, 0.61f, 0.20f), gamma);
	colors[ImGuiCol_ModalWindowDimBg]       = glm::pow(glm::vec4(0.22f, 0.22f, 0.22f, 0.35f), gamma);
	colors[ImGuiCol_Header]                 = glm::pow(glm::vec4(0.17f, 0.17f, 0.17f, 1.0f), gamma);

	colors[ImGuiCol_Text]                   = textColor;
	colors[ImGuiCol_CheckMark]              = textColor;
	colors[ImGuiCol_WindowBg]               = bgColor;
	colors[ImGuiCol_TitleBg]                = bgColor;
	colors[ImGuiCol_TitleBgCollapsed]       = bgColor;
	colors[ImGuiCol_ScrollbarGrab]          = buttonColor;
	colors[ImGuiCol_ScrollbarGrabHovered]   = buttonHoverColor;
	colors[ImGuiCol_ScrollbarGrabActive]    = buttonActiveColor;
	colors[ImGuiCol_SliderGrab]             = buttonColor;
	colors[ImGuiCol_SliderGrabActive]       = buttonActiveColor;
	colors[ImGuiCol_Button]                 = buttonColor;
	colors[ImGuiCol_ButtonHovered]          = buttonHoverColor;
	colors[ImGuiCol_ButtonActive]           = buttonActiveColor;
	colors[ImGuiCol_HeaderHovered]          = buttonHoverColor;
	colors[ImGuiCol_HeaderActive]           = buttonActiveColor;
	colors[ImGuiCol_Separator]              = buttonColor;
	colors[ImGuiCol_SeparatorHovered]       = buttonHoverColor;
	colors[ImGuiCol_SeparatorActive]        = buttonActiveColor;
	colors[ImGuiCol_ResizeGrip]             = buttonColor;
	colors[ImGuiCol_ResizeGripHovered]      = buttonHoverColor;
	colors[ImGuiCol_ResizeGripActive]       = buttonActiveColor;
	colors[ImGuiCol_Tab]                    = buttonColor;
	colors[ImGuiCol_TabHovered]             = buttonHoverColor;
	colors[ImGuiCol_TextSelectedBg]         = buttonColor;
	colors[ImGuiCol_DragDropTarget]         = textColor;

	// Frames.
	style.FrameRounding		 = 5.0f;
	style.GrabRounding		 = 3.0f;
	style.WindowRounding	 = 5.0f;
	style.ScrollbarRounding  = 12.0f;
	style.ScrollbarSize		 = 12.0f;
	style.WindowTitleAlign.x = 0.5f;
	style.FramePadding.y	 = 4.0f;
	style.ItemSpacing.y		 = 3.0f;
}

bool Window::showDirectoryPicker(const fs::path & startDir, fs::path & outPath) {

	char* outPathStr = nullptr;
	int res = sr_gui_ask_directory("Select directory", startDir.string().c_str(), &outPathStr);

	if(res == SR_GUI_VALIDATED){
		outPath = fs::path(outPathStr);
		free(outPathStr);
		return true;
	}
	if(outPathStr != nullptr){
		free(outPathStr);
	}
	return false;
}
