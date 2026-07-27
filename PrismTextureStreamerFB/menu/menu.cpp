#include "menu.h"
#include <d3d11.h>
#include <map>

#include "../version.h"

#include "../scs_logging.h"
using namespace scs_logging;

#include "../prism/prism.h"
#include "../dx11/present.h"
#include "../dinput8/dinput8.h"

#include <ImGui/imgui.h>
#include "../misc/imgui_stdlib.h"
#include <ImGui/imgui_impl_dx11.h>
#include <ImGui/imgui_impl_win32.h>

#include "../screens.h"
#include "../sources/window.h"
#include "../sources/wgc_window.h"


static bool menu_visible{};
void on_frame()
{
	static bool wasPressed = false;

	bool ctrlDown = GetAsyncKeyState(VK_CONTROL) & 0x8000;
	bool f8Down = GetAsyncKeyState(VK_F8) & 0x8000;
	bool isPressed = ctrlDown && f8Down;

	if (isPressed && !wasPressed) {
		menu_visible = !menu_visible;
		dinput8::set_mouse(menu_visible);

		if (ImGui::GetCurrentContext()) {
			ImGuiIO* io = &ImGui::GetIO();
			if (io) io->MouseDrawCursor = menu_visible;
		}
	}
	wasPressed = isPressed;


	if (menu_visible) {
		ImGui::SetNextWindowSizeConstraints(ImVec2(584, 208), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::Begin(("Prism3D Texture Streamer v" + std::string(g_version)).c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

		static bool unsavedChanges = false;
		static bool hasGps = false;
		static bool hasDash = false;
		// static bool hasGps = false; // More than 1 custom IS possible..

		ImGui::BeginDisabled(hasGps);
		if (ImGui::Button("Add Screen GPS"))
		{
			unsavedChanges = true;

			screen_t screen;
			screen.type = screen_type_t::GPS;
			screen.original_texture = "/vehicle/truck/share/gps.tobj";
			screen.override_texture = "/home/PrismTextureStreamer/gps.tobj";
			screen.override_texture_size_h = 2048;
			screen.override_texture_size_w = 64;

			{
				std::lock_guard<std::mutex> lock(g_screens_mutex);
				g_screens.push_back(std::move(screen));
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(hasDash);
		if (ImGui::Button("Add Screen Dashboard"))
		{
			unsavedChanges = true;

			screen_t screen;
			screen.type = screen_type_t::DASHBOARD;
			screen.original_texture = "/vehicle/truck/share/dashboard.tobj";
			screen.override_texture = "/home/PrismTextureStreamer/dashboard.tobj";
			screen.override_texture_size_h = 64;
			screen.override_texture_size_w = 2048;

			{
				std::lock_guard<std::mutex> lock(g_screens_mutex);
				g_screens.push_back(std::move(screen));
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Add Screen Custom"))
		{
			unsavedChanges = true;

			screen_t screen;
			screen.type = screen_type_t::CUSTOM;
			screen.original_texture = ".tobj";
			screen.override_texture = "/home/PrismTextureStreamer/.tobj";

			{
				std::lock_guard<std::mutex> lock(g_screens_mutex);
				g_screens.push_back(std::move(screen));
			}
		}
		ImGui::SameLine();

		if (unsavedChanges) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.10f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.18f, 0.08f, 1.0f));
		}

		ImGui::BeginDisabled(!unsavedChanges);
		bool justSaved = false;
		if (ImGui::Button("Apply Unsaved Changes"))
		{
			prism::string cmd("game");
			prism::execute_command::call(&cmd, -1);

			justSaved = true;
			unsavedChanges = false;
		}
		ImGui::EndDisabled();

		if (unsavedChanges || justSaved) ImGui::PopStyleColor(3);




		std::map<std::string, std::string> applications; // window title, application name
		EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
			auto* applications = reinterpret_cast<std::map<std::string, std::string>*>(lParam);

			if (!IsWindowVisible(hwnd))
				return TRUE;

			LONG exStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
			if (exStyle & WS_EX_TOOLWINDOW)
				return TRUE;

			std::string windowTitle;
			bool hasTitle{};

			int titleLen = GetWindowTextLengthA(hwnd);
			if (titleLen != 0)
			{
				hasTitle = true;

				windowTitle = std::string(titleLen + 1, '\0');
				GetWindowTextA(hwnd, windowTitle.data(), titleLen + 1);
				windowTitle.resize(titleLen);
			}



			std::string applicationName;

			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);

			HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (!hProc) return TRUE; // Continue, dont add if no process name

			char path[MAX_PATH]{};
			DWORD size = MAX_PATH;
			QueryFullProcessImageNameA(hProc, 0, path, &size);
			CloseHandle(hProc);

			applicationName = std::string(path);

			auto pos = applicationName.rfind('\\');
			applicationName = pos != std::string::npos ? applicationName.substr(pos + 1) : applicationName;

			if (!hasTitle || windowTitle.empty() || windowTitle == "")
				windowTitle = applicationName;

			(*applications)[windowTitle] = applicationName;

			return TRUE;
		}, reinterpret_cast<LPARAM>(&applications));


		{
			std::lock_guard<std::mutex> lock(g_screens_mutex);
			std::vector<int> to_remove{}; // indexes to remove

			int i = 0;
			hasGps = false;
			hasDash = false;
			for (screen_t& screen : g_screens) {
				bool isGps		= screen.type == screen_type_t::GPS;
				bool isDash		= screen.type == screen_type_t::DASHBOARD;
				bool isCustom	= screen.type == screen_type_t::CUSTOM;

				if (isGps)
					hasGps = true;
				else if (isDash)
					hasDash = true;


				std::string name = isGps ? "GPS##" : (isDash ? "Dashboard##" : "Custom");
				if (ImGui::CollapsingHeader((name + std::to_string(i)).c_str()))
				{
					ImGui::PushID(i);

					if (isCustom) {
						if (ImGui::BeginTable("screen_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
						{
							ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 170.0f);
							ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Game Screen Texture");
							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(-FLT_MIN);
							ImGui::InputText("##original_texture", &screen.original_texture);


							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Unqiue Override Texture");
							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(-FLT_MIN);
							ImGui::InputText("##override_texture", &screen.override_texture);


							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Override Texture Width");
							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(-FLT_MIN);
							ImGui::InputScalar("##override_texture_size_w", ImGuiDataType_U32, &screen.override_texture_size_w);


							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Override Texture Height");
							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(-FLT_MIN);
							ImGui::InputScalar("##override_texture_size_h", ImGuiDataType_U32, &screen.override_texture_size_h);

							ImGui::EndTable();
						}
					}

					ImGui::Text("Resolution");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80.0f);
					if (ImGui::InputScalar("##res_w", ImGuiDataType_U32, &screen.targetLiveTextureWidth)) unsavedChanges = true;
					ImGui::SameLine();
					ImGui::Text("x");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80.0f);
					if (ImGui::InputScalar("##res_h", ImGuiDataType_U32, &screen.targetLiveTextureHeight)) unsavedChanges = true;

					ImGui::BeginDisabled(!screen.legacyCapture);
					ImGui::Text("Framerate");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(160.0f);
					uint8_t fps_min = 1, fps_max = 255;
					if (ImGui::SliderScalar("##target_fps", ImGuiDataType_U8, &screen.framerate, &fps_min, &fps_max) && screen.source.get()) {
						screen.source->SetFramerate(screen.framerate);
					}
					ImGui::EndDisabled();

					ImGui::Checkbox("Legacy Capture", &screen.legacyCapture);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Not Recommended | Only use if capture fails");
						ImGui::EndTooltip();
					}

					// Nothing can be selected without a source (handles for application closing)
					if (!screen.source.get()) {
						screen.source_application_display_name.clear();
						screen.source_application_name.clear();
					}

					const char* preview = screen.source_application_name.empty() ? "Select Source..." : screen.source_application_name.c_str();

					bool changed = false;
					if (ImGui::BeginCombo("##appcombo", preview))
					{
						int i = 0;
						for (const auto& [title, application] : applications) {
							bool selected = (title == screen.source_application_display_name) ? true : ((application == screen.source_application_name) ? true : false);

							if (ImGui::Selectable(("[" + application + "] " + title + "##" + std::to_string(i)).c_str(), selected)) {
								screen.source_application_name = application;
								screen.source_application_display_name = title;
								changed = true;
							}

							if (selected)
								ImGui::SetItemDefaultFocus();

							i++;
						}
						ImGui::EndCombo();
					}

					if (changed) {
						g_screen_source_creation_in_progress = true;
						screen.source.reset(); // Destroy before construct

						if (screen.legacyCapture) {
							screen.source = sources::CreateWindowSource(screen.source_application_name.c_str(), screen.source_application_display_name.c_str());
						}
						else {
							screen.source = sources::CreateWgcWindowSource(screen.source_application_name.c_str(), screen.source_application_display_name.c_str());
						}

						if (!screen.source.get()) {
							screen.source_application_display_name.clear();
							screen.source_application_name.clear();
							ImGui::OpenPopup("Source Error");
						}

						g_screen_source_creation_in_progress = false;
					}

					if (ImGui::BeginPopupModal("Source Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						ImGui::TextWrapped("Failed to use source, check the console for more details");
						if (ImGui::Button("OK", ImVec2(120, 0))) {
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}


					if (ImGui::Button("Remove")) {

						if (screen.liveTexture) screen.liveTexture->Release();
						if (screen.immediateContext) screen.immediateContext->Release();

						to_remove.push_back(i);
					}
					ImGui::SameLine();
					if (ImGui::Button("Flip Screen")) {
						screen.flipVertical = !screen.flipVertical;
					}

					ImGui::PopID();
				}

				i++;
			}

			for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
				g_screens.erase(g_screens.begin() + *it);
			}
		}




		ImGui::End();
	}
}

namespace Gui {
	bool init()
	{
		dx11::present::on_frame(on_frame);
		return true;
	}
}
