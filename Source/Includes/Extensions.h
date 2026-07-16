#define ScreenWidth  [UIScreen mainScreen].bounds.size.width
#define ScreenHeight [UIScreen mainScreen].bounds.size.height


inline float lerp(float a, float b, float f) {
	return std::clamp(a + f * (b - a),a > b ? b : a,a > b ? a : b);
}

inline ImColor collerp(ImColor a, ImColor b, float f) {
  return {a.Value.x + f * (b.Value.x - a.Value.x), a.Value.y + f * (b.Value.y - a.Value.y), a.Value.z + f * (b.Value.z - a.Value.z), a.Value.w + f * (b.Value.w - a.Value.w)};
}


void Draw3DBox(Vector3 pos, ImColor color, float stroke = 2, float outline_size = 0, float cornersize = 50) {
    cornersize /= 10;
    bool checker;
    Camera$$WorldToScreen::Checker(pos,checker);
    if (!checker) return;
 Vector3 top_leftup1 = pos + Vector3(0.4, 1.8, 0.6);
 Vector3 top_leftbottom1 = pos + Vector3(0.4, 0, 0.6);
 Vector3 top_rightbottom1 = pos + Vector3(-0.3, 0, 0.6);
 Vector3 top_rightup1 = pos + Vector3(-0.3, 1.8, 0.6);
 Vector3 down_leftup1 = (pos + Vector3(0.4, 1.8, 0)) + Vector3(0, 0, -0.4);
 Vector3 down_leftbottom1 = (pos + Vector3(0.4, 0, 0)) + Vector3(0, 0, -0.4);
 Vector3 down_rightbottom1 = (pos + Vector3(-0.3, 0, 0)) + Vector3(0, 0, -0.4);
 Vector3 down_rightup1 = (pos + Vector3(-0.3, 1.8, 0)) + Vector3(0, 0, -0.4);

    ImVec2 top_leftup = Camera$$WorldToScreen::Regular(top_leftup1);
    ImVec2 top_leftbottom = Camera$$WorldToScreen::Regular(top_leftbottom1);
    ImVec2 top_rightbottom =Camera$$WorldToScreen::Regular(top_rightbottom1);
    ImVec2 top_rightup = Camera$$WorldToScreen::Regular(top_rightup1);
    ImVec2 down_leftup = Camera$$WorldToScreen::Regular(down_leftup1);
    ImVec2 down_leftbottom = Camera$$WorldToScreen::Regular(down_leftbottom1);
    ImVec2 down_rightbottom = Camera$$WorldToScreen::Regular(down_rightbottom1);
    ImVec2 down_rightup = Camera$$WorldToScreen::Regular(down_rightup1);

    auto old_col = color;
    stroke += outline_size;
    color = ImColor(0,0,0,255);
    

    ImGui::GetBackgroundDrawList()->AddLine({top_leftup.x, top_leftup.y}, {top_leftbottom.x, top_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightbottom.x, top_rightbottom.y}, {top_leftbottom.x, top_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_leftup.x, top_leftup.y}, {top_rightup.x, top_rightup.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightup.x, top_rightup.y}, {top_rightbottom.x, top_rightbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({down_leftup.x, down_leftup.y}, {down_leftbottom.x, down_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({down_rightbottom.x, down_rightbottom.y}, {down_leftbottom.x, down_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({down_leftup.x, down_leftup.y}, {down_rightup.x, down_rightup.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({down_rightup.x, down_rightup.y}, {down_rightbottom.x, down_rightbottom.y}, color, stroke);

    ImGui::GetBackgroundDrawList()->AddLine({top_leftup.x, top_leftup.y}, {down_leftup.x, down_leftup.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_leftbottom.x, top_leftbottom.y}, {down_leftbottom.x, down_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightbottom.x, top_rightbottom.y}, {down_rightbottom.x, down_rightbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightup.x, top_rightup.y}, {down_rightup.x, down_rightup.y}, color, stroke);

    stroke = outline_size;
    color = old_col;

    ImGui::GetBackgroundDrawList()->AddLine({top_leftup.x, top_leftup.y}, {top_leftbottom.x, top_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightbottom.x, top_rightbottom.y}, {top_leftbottom.x, top_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_leftup.x, top_leftup.y}, {top_rightup.x, top_rightup.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightup.x, top_rightup.y}, {top_rightbottom.x, top_rightbottom.y}, color, stroke);

    ImGui::GetBackgroundDrawList()->AddLine({down_leftup.x, down_leftup.y}, {down_leftbottom.x, down_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({down_rightbottom.x, down_rightbottom.y}, {down_leftbottom.x, down_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({down_leftup.x, down_leftup.y}, {down_rightup.x, down_rightup.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({down_rightup.x, down_rightup.y}, {down_rightbottom.x, down_rightbottom.y}, color, stroke);

    ImGui::GetBackgroundDrawList()->AddLine({top_leftup.x, top_leftup.y}, {down_leftup.x, down_leftup.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_leftbottom.x, top_leftbottom.y}, {down_leftbottom.x, down_leftbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightbottom.x, top_rightbottom.y}, {down_rightbottom.x, down_rightbottom.y}, color, stroke);
    ImGui::GetBackgroundDrawList()->AddLine({top_rightup.x, top_rightup.y}, {down_rightup.x, down_rightup.y}, color, stroke);
}

void drawlineglow(ImDrawList* draw, ImVec2 start, ImVec2 end, ImColor col, int thickness, int size) {
    draw->AddLine(start, end, col, thickness);
    for (int i = 0; i < size; i++) {
        if (start.y != end.y)
            draw->AddLine({start.x - i/2, start.y}, {end.x + i/2, end.y}, ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w * (1.0f / (float) size) * (((float) (size - i)) / (float) size)), thickness+i);
        else
            draw->AddRectFilled({start.x - i/2, start.y - i/2}, {end.x + i/2, end.y + i/2}, ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w * (1.0f / (float) size) * (((float) (size - i)) / (float) size)), 4+i);
    }
}
void Draw3DCircle(Vector3 pos, float radius, float stroke, ImColor color, float segments, bool filled = false, float fillopacity = 0) {
    void* camera = game_sdk->get_camera();
    if (!camera) return;
    ImVec2 vCenter = Camera$$WorldToScreen::Regular(pos);
    for (float i = 0; i < segments; i++) {
        if (i < segments) {
            Vector3 pos1 = Vector3(pos.x + radius * cos(i * (PI * 2) / segments),pos.y,pos.z + radius * sin(i * (PI * 2) / segments));
            Vector3 pos2 = Vector3(pos.x + radius * cos((i + 1) * (PI * 2) / segments),pos.y,pos.z + radius * sin((i + 1) * (PI * 2) / segments));
            bool checker1;
            bool checker2;
            ImVec2 vPos = Camera$$WorldToScreen::Checker(pos1, checker1);
            ImVec2 vNextPos = Camera$$WorldToScreen::Checker(pos2, checker2);
            if (checker1 && checker2) {
                drawlineglow(ImGui::GetBackgroundDrawList(), vPos, vNextPos, color, stroke, stroke+4);
                if (filled) {
                    ImGui::GetBackgroundDrawList()->PathLineTo(vPos);
                    ImGui::GetBackgroundDrawList()->PathLineTo(vNextPos);
                }
            }
        }
    }
}