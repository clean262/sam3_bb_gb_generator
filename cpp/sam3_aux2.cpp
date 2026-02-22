// v0.0.7
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <excpt.h>

#include "aviutl2_sdk/plugin2.h" // SDK mirror の include/aviutl2_sdk/plugin2.h

// ---- 簡易ユーティリティ ----
namespace fs = std::filesystem;
static HMODULE g_hmod = nullptr;

static std::wstring GetModulePathW(HMODULE hmod) {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(hmod, buf, MAX_PATH);
    return std::wstring(buf);
}

static std::wstring GetModuleDirW(HMODULE hmod) {
    fs::path p = GetModulePathW(hmod);
    return p.parent_path().wstring();
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

static std::string JsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 16);
    for (char c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", (unsigned char)c);
                out += buf;
            } else out += c;
        }
    }
    return out;
}

static bool WriteTextFileUtf8Atomic(const fs::path& path, const std::string& text) {
    fs::path tmp = path;
    tmp += L".tmp";

    {
        std::ofstream ofs(tmp, std::ios::binary);
        if (!ofs) return false;
        ofs.write(text.data(), (std::streamsize)text.size());
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (!ec) return true;

    // 既存がある場合は置換
    fs::remove(path, ec);
    ec.clear();
    fs::rename(tmp, path, ec);
    return !ec;
}

static std::optional<std::string> ReadAllText(const fs::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return std::nullopt;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}


static bool FileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static uintmax_t FileSizeSafe(const fs::path& p) {
    std::error_code ec;
   auto s = fs::file_size(p, ec);
   return ec ? 0 : s;
}

static double GetProjectFps(EDIT_SECTION* edit) {
    double fps = 30.0;
    if (edit && edit->info && edit->info->scale != 0) {
        fps = (double)edit->info->rate / (double)edit->info->scale;
    }
    if (fps <= 0.0) fps = 30.0;
    return fps;
}

static void AppendLog(const fs::path& jobdir, const std::wstring& msg);
static void LogObjectRange(EDIT_SECTION* edit, const fs::path& jobdir, const wchar_t* tag, OBJECT_HANDLE obj);
static void LogItemValue(EDIT_SECTION* edit, const fs::path& jobdir, OBJECT_HANDLE obj,
    const wchar_t* eff, const wchar_t* item, const wchar_t* tag);
static bool SetItemValueLogged(EDIT_SECTION* edit, const fs::path& jobdir, OBJECT_HANDLE obj,
    const wchar_t* eff, const wchar_t* item, const char* value, const wchar_t* tag);

// ---- alias(UTF-8) 解析ユーティリティ ----
static inline bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && 0 == s.compare(0, prefix.size(), prefix);
}

static inline void RStripCR(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
}

static inline std::string TrimAscii(std::string s) {
    auto issp = [](unsigned char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && issp((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && issp((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::wstring HexU32W(DWORD v) {
    wchar_t buf[32]{};
    swprintf_s(buf, L"0x%08X", (unsigned)v);
    return buf;
}
// [Object] セクションの frame=... を frame=0,<len> に正規化する
// 目的:
//  - create_object_from_alias は alias 内 frame 情報で length が上書きされうるため、
//    常に “こちらが意図した長さ” に揃える。
static std::string NormalizeAliasObjectFrameHeader(
    const std::string& original_alias_utf8,
    int expected_length
) {
    std::string alias = original_alias_utf8;

    // BOM除去（念のため）
    if (alias.size() >= 3 &&
        (unsigned char)alias[0] == 0xEF &&
        (unsigned char)alias[1] == 0xBB &&
        (unsigned char)alias[2] == 0xBF) {
        alias.erase(0, 3);
    }

    if (expected_length < 1) expected_length = 1;

    std::istringstream iss(alias);
    std::ostringstream out;
    std::string line;

    bool in_object_header = false;   // [Object] セクション内か
    bool replaced = false;           // frame= を置換したか

    while (std::getline(iss, line)) {
        RStripCR(line);
        std::string t = TrimAscii(line);

        if (t == "[Object]") {
            in_object_header = true;
            out << line << "\r\n";
            continue;
        }

        // 次のセクションに入る前に frame 行が無ければ挿入
        if (in_object_header && !t.empty() && t.front() == '[') {
            if (!replaced) {
                out << "frame=0," << expected_length << "\r\n";
                replaced = true;
            }
            in_object_header = false;
            out << line << "\r\n";
            continue;
        }

        if (in_object_header && StartsWith(t, "frame=")) {
            out << "frame=0," << expected_length << "\r\n";
            replaced = true;
            continue;
        }

        out << line << "\r\n";
    }

    // ファイル末尾まで [Object] セクションが続いていて frame が無い場合
    if (in_object_header && !replaced) {
        out << "frame=0," << expected_length << "\r\n";
    }

    return out.str();
}

static bool ParseTwoDoublesFromCsvPrefix(const std::string& s_in, double& a, double& b) {
    // 例: "0.000,23.867,再生範囲,0" から先頭2つを読む
    std::string s = TrimAscii(s_in);

    size_t c1 = s.find(',');
    if (c1 == std::string::npos) return false;
    size_t c2 = s.find(',', c1 + 1);

    std::string t1 = TrimAscii(s.substr(0, c1));
    std::string t2 = TrimAscii(c2 == std::string::npos ? s.substr(c1 + 1) : s.substr(c1 + 1, c2 - (c1 + 1)));

    if (t1.empty() || t2.empty()) return false;

    char* e1 = nullptr;
    char* e2 = nullptr;
    a = std::strtod(t1.c_str(), &e1);
    b = std::strtod(t2.c_str(), &e2);
    if (e1 == t1.c_str() || e2 == t2.c_str()) return false;
    return true;
}

static bool IsVideoFileEffectName(const std::string& eff) {
    const std::string ja_video_file = WideToUtf8(L"\u52d5\u753b\u30d5\u30a1\u30a4\u30eb"); // 動画ファイル
    const std::string ja_video = WideToUtf8(L"\u52d5\u753b"); // 動画
    if (eff.find(ja_video_file) != std::string::npos) return true;
    if (eff.find("Video File") != std::string::npos) return true;
    if (eff.find(ja_video) != std::string::npos) return true;
    return false;
}

struct AliasVideoInfo {
    bool has_file = false;
    bool has_playback = false;
    std::string file_utf8;
    double playback_start = 0.0;
    double playback_end = 0.0;
};

static AliasVideoInfo ParseVideoInfoFromAliasUtf8(std::string alias_utf8) {
    AliasVideoInfo out;

    // BOM除去（念のため）
    if (alias_utf8.size() >= 3 &&
        (unsigned char)alias_utf8[0] == 0xEF &&
        (unsigned char)alias_utf8[1] == 0xBB &&
        (unsigned char)alias_utf8[2] == 0xBF) {
        alias_utf8.erase(0, 3);
    }

    std::istringstream iss(alias_utf8);
    std::string line;

    bool in_video_effect = false;

    while (std::getline(iss, line)) {
        RStripCR(line);

        // セクション開始でリセット（[Object.0] など）
        if (!line.empty() && line.front() == '[') {
            in_video_effect = false;
            continue;
        }

        // effect.name=動画ファイル
        const std::string kEff1 = "effect.name=";
        if (StartsWith(line, kEff1)) {
            std::string eff = line.substr(kEff1.size());
            eff = TrimAscii(eff);
            in_video_effect = IsVideoFileEffectName(eff);
            continue;
        }

        if (!in_video_effect) continue;

        // ファイル=...
        // （日本語/英語の両方を拾う）
        // （中略）
        const std::string ja_file_key = WideToUtf8(L"\u30d5\u30a1\u30a4\u30eb") + "="; // ファイル=
        const std::vector<std::string> fileKeys = { ja_file_key, "File=", "file=", "path=", "Path=" };
        for (const auto& k : fileKeys) {
            if (StartsWith(line, k)) {
                std::string v = line.substr(k.size());
                v = TrimAscii(v);
                if (!v.empty()) {
                    out.has_file = true;
                    out.file_utf8 = v;
                }
                break;
            }
        }

        // 再生位置=開始,終了,再生範囲,0
        const std::string ja_playback_key = WideToUtf8(L"\u518d\u751f\u4f4d\u7f6e") + "="; // 再生位置=
        const std::vector<std::string> playKeys = { ja_playback_key, "Playback=", "playback=" };
        for (const auto& k : playKeys) {
            if (StartsWith(line, k)) {
                std::string v = line.substr(k.size());
                double a = 0.0, b = 0.0;
                if (ParseTwoDoublesFromCsvPrefix(v, a, b)) {
                    out.has_playback = true;
                    out.playback_start = a;
                    out.playback_end = b;
                }
                break;
            }
        }

        // もう必要情報が揃ったら抜けてもOK
        if (out.has_file && out.has_playback) break;
    }

    return out;
}

// ---- Apply: alias を “出力 mp4” に差し替えて新規オブジェクト作成 ----

static std::string PatchAliasReplaceVideoFilePath(
    const std::string& original_alias_utf8,
    const std::string& new_video_path_utf8,
    bool* replaced_out = nullptr
) {
    // 元aliasの「動画ファイル」effect の中にある ファイル=... だけ差し替える
    // ついでに「再生位置=...」は削除（出力mp4は既に切り出し済みなのでズレの原因になりがち）
    std::string alias = original_alias_utf8;

    // BOM除去
    if (alias.size() >= 3 &&
        (unsigned char)alias[0] == 0xEF &&
        (unsigned char)alias[1] == 0xBB &&
        (unsigned char)alias[2] == 0xBF) {
        alias.erase(0, 3);
    }

    std::istringstream iss(alias);
    std::ostringstream out;

    std::string line;
    bool in_video_effect = false;
    bool replaced_any = false;

    const std::string kEff = "effect.name=";
    const std::string ja_file_key = WideToUtf8(L"\u30d5\u30a1\u30a4\u30eb") + "="; // ファイル=
    const std::string ja_playback_key = WideToUtf8(L"\u518d\u751f\u4f4d\u7f6e") + "="; // 再生位置=
    const std::vector<std::string> fileKeys = { ja_file_key, "File=", "file=", "path=", "Path=" };
    const std::vector<std::string> playKeys = { ja_playback_key, "Playback=", "playback=" };

    while (std::getline(iss, line)) {
        RStripCR(line);

        std::string trimmed = TrimAscii(line);

        // セクション境界で動画判定を切る（[Object.0] / [Effect.0] など）
        if (!trimmed.empty() && trimmed.front() == '[') {
            in_video_effect = false;
            out << line << "\r\n";
            continue;
        }

        // effect.name=...
        if (StartsWith(trimmed, kEff)) {
            std::string eff = TrimAscii(trimmed.substr(kEff.size()));
            in_video_effect = IsVideoFileEffectName(eff);
            out << line << "\r\n";
            continue;
        }

        if (in_video_effect) {
            // ファイル=... を差し替え
            bool did = false;
            for (const auto& k : fileKeys) {
                if (StartsWith(trimmed, k)) {
                    // 元のキーを優先して書く（日本語/英語どっちでもOK）
                    out << k << new_video_path_utf8 << "\r\n";
                    replaced_any = true;
                    did = true;
                    break;
                }
            }
            if (did) continue;

            // 再生位置=... は削除（不要・ズレ要因）
            for (const auto& k : playKeys) {
                if (StartsWith(trimmed, k)) {
                    did = true;
                    break;
                }
            }
            if (did) continue;
        }

        out << line << "\r\n";
    }

    // もし置換できなかった（=動画ファイルeffect見つからない等）なら、最悪元aliasを返す
    // （この場合 Apply は失敗する可能性が高いので、呼び出し側で検出してメッセージ出す）
    if (replaced_out) *replaced_out = replaced_any;
    if (!replaced_any) return original_alias_utf8;
    return out.str();
}

static int FindMaxObjectIndexInAlias(const std::string& alias_utf8) {
    std::istringstream iss(alias_utf8);
    std::string line;
    int mx = -1;
    while (std::getline(iss, line)) {
        RStripCR(line);
        std::string t = TrimAscii(line);
        if (!StartsWith(t, "[Object.")) continue;
        auto r = t.find(']');
        if (r == std::string::npos) continue;
        std::string inside = t.substr(std::string("[Object.").size(), r - std::string("[Object.").size());
        // 数字だけパース（Object.Something みたいなのは無視）
        int v = 0;
        bool ok = !inside.empty();
        for (char c : inside) {
            if (c < '0' || c > '9') { ok = false; break; }
            v = v * 10 + (c - '0');
        }
        if (ok) mx = std::max(mx, v);
    }
    return mx;
}

// ---- Insert mode (透過 / GB / BB) ----
enum class InsertMode {
    Transparent = 0,
    GB = 1,
    BB = 2,
};
static std::string InsertModeToUtf8(InsertMode m) {
    switch (m) {
    case InsertMode::Transparent: return "transparent";
    case InsertMode::BB: return "BB";
    default: return "GB";
    }
}

static std::string BgModeFromInsertMode(InsertMode m) {
    // 透過はbgを使わないが request.json 側の整合のため既定GBを返す
    if (m == InsertMode::BB) return "BB";
    return "GB";
}
static InsertMode InsertModeFromUtf8(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    if (s == "bb") return InsertMode::BB;
    if (s == "gb") return InsertMode::GB;
    if (s == "transparent" || s == "alpha" || s == "a") return InsertMode::Transparent;
    return InsertMode::GB;
}

// alias に「マスク適用（α化）」効果を upsert し、mask のパスを設定する
// 
static std::string PatchAliasUpsertAlphaMaskEffect(
    const std::string& original_alias_utf8,
    const std::string& mask_video_path_utf8,
    double mask_src_start_sec,
    double mask_src_end_sec
) {
    // effect.name=SAM3mask を upsert し、マスク動画のファイルパスを設定する。
    // Note: .anm2 の `--file@var:ラベル` は、alias 側の保存キーが
    //       `var` ではなく「ラベル（コロン右側）」になる挙動がある。
    //       例) --file@path:動画ファイル  -> alias は "動画ファイル=..."
    //       よって本件は "マスク動画ファイル=..." を書く必要がある。
    auto fmt_sec = [](double v) -> std::string {
        char buf[64]{};
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        return std::string(buf);
    };
    const std::string s_start = fmt_sec(mask_src_start_sec);
    const std::string s_end   = fmt_sec(mask_src_end_sec);
    std::string alias = original_alias_utf8;
    if (alias.size() >= 3 &&
        (unsigned char)alias[0] == 0xEF &&
        (unsigned char)alias[1] == 0xBB &&
        (unsigned char)alias[2] == 0xBF) {
        alias.erase(0, 3);
    }

    std::istringstream iss(alias);
    std::ostringstream out;
    std::string line;

    const std::string kEff = "effect.name=";
    const std::string targetEffect = "SAM3mask";

    // "マスク動画ファイル" は SAM3mask.anm2 の --file@path:マスク動画ファイル に対応
    const std::string preferredKey = WideToUtf8(L"\u30de\u30b9\u30af\u52d5\u753b\u30d5\u30a1\u30a4\u30eb") + "=";
    const std::string preferredPathVarKey = "path=";
    // SAM3mask.anm2 の --value@mask_src_start/end のラベルに対応
    const std::string preferredStartKey = WideToUtf8(L"\u5143\u52d5\u753b\u958b\u59cb\u79d2") + "=";
    const std::string preferredEndKey   = WideToUtf8(L"\u5143\u52d5\u753b\u7d42\u4e86\u79d2") + "=";
    const std::string preferredStartVarKey = "mask_src_start=";
    const std::string preferredEndVarKey   = "mask_src_end=";
    const std::vector<std::string> fileKeys = {
        preferredKey,
        preferredPathVarKey,
        preferredStartKey,
        preferredEndKey,
        preferredStartVarKey,
        preferredEndVarKey,
        WideToUtf8(L"\u30d5\u30a1\u30a4\u30eb") + "=", // ファイル=
        "path=", "Path=", "file=", "File=",
        "mask_path=", "MaskPath=", "mask=", "Mask=",
        "mask_src_start=", "mask_src_end="
    };

    bool in_target_effect = false;
    bool found_target = false;

    while (std::getline(iss, line)) {
        RStripCR(line);
        std::string trimmed = TrimAscii(line);

        // セクション境界
        if (!trimmed.empty() && trimmed.front() == '[') {
            in_target_effect = false;
            out << line << "\r\n";
            continue;
        }

        // effect.name=...
        if (StartsWith(trimmed, kEff)) {
            std::string eff = TrimAscii(trimmed.substr(kEff.size()));
            bool is_target = (eff == targetEffect);
            in_target_effect = is_target;
            if (is_target) {
                found_target = true;
            }
            out << line << "\r\n";

            if (is_target) {
                // 堅牢性の為表示ラベルと変数名キーを記述
                out << preferredKey << mask_video_path_utf8 << "\r\n";
                out << preferredPathVarKey << mask_video_path_utf8 << "\r\n";
                out << preferredStartKey << s_start << "\r\n";
                out << preferredEndKey   << s_end   << "\r\n";
                out << preferredStartVarKey << s_start << "\r\n";
                out << preferredEndVarKey   << s_end   << "\r\n";
            }
            continue;
        }

        // ターゲット効果内の既存 file 行は捨てる（重複防止）
        if (in_target_effect) {
            for (const auto& k : fileKeys) {
                if (StartsWith(trimmed, k)) {
                    // skip
                    goto CONTINUE_LOOP;
                }
            }
        }

        out << line << "\r\n";
    CONTINUE_LOOP:
        ;
    }

    // 効果が無かった場合は末尾に追加
    if (!found_target) {
        int next = FindMaxObjectIndexInAlias(alias) + 1;
        if (next < 1) next = 1;
        out << "\r\n";
        out << "[Object." << next << "]\r\n";
        out << "effect.name=" << targetEffect << "\r\n";
        out << preferredKey << mask_video_path_utf8 << "\r\n";
        out << preferredPathVarKey << mask_video_path_utf8 << "\r\n";
        out << preferredStartKey << s_start << "\r\n";
        out << preferredEndKey   << s_end   << "\r\n";
        out << preferredStartVarKey << s_start << "\r\n";
        out << preferredEndVarKey   << s_end   << "\r\n";
    }

    return out.str();
}

// ------------------------------------------------------------
// Hide source object without deleting it:
//   - set "映像再生"->"透明度" to 100.00 (fully transparent)
//   - disable audio to avoid double audio ("動画ファイル"->"音声付き"=0)
// ------------------------------------------------------------
static std::string FormatFloat2(double v) {
	char buf[64]{};
	std::snprintf(buf, sizeof(buf), "%.2f", v);
	return std::string(buf);
}

static void HideObjectByOpacityAndMute(EDIT_SECTION* edit, const fs::path& jobdir, OBJECT_HANDLE obj, int frame_hint) {
	if (!edit || !obj) return;

    // IMPORTANT:
    // Many timeline editors apply "item value" at current cursor frame.
    // If cursor is at frame 0 but object starts later, the write may not affect the object.
    // So: move cursor inside the object range, write values, then restore cursor.
    int old_frame = (edit->info ? edit->info->frame : 0);
    int old_layer = (edit->info ? edit->info->layer : 0);

    OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(obj);
    int target_frame = frame_hint;
    if (target_frame < lf.start) target_frame = lf.start;
    if (target_frame >= lf.end)  target_frame = lf.end - 1;
    if (target_frame < 0) target_frame = 0;

    AppendLog(jobdir,
        std::wstring(L"[hide] cursor(old) layer=") + std::to_wstring(old_layer) +
        L" frame=" + std::to_wstring(old_frame) +
        L" -> cursor(new) layer=" + std::to_wstring(lf.layer) +
        L" frame=" + std::to_wstring(target_frame));

    edit->set_cursor_layer_frame(lf.layer, target_frame); // clampされる :contentReference[oaicite:1]{index=1}

	// 1) Visual hide
    const std::string op = FormatFloat2(100.0);
    SetItemValueLogged(edit, jobdir, obj, L"映像再生", L"透明度", op.c_str(), L"hide");

	// 2) Mute: do both "音声付き=0" and "音量=0" (more robust)
    SetItemValueLogged(edit, jobdir, obj, L"動画ファイル", L"音声付き", "0", L"hide");
    const std::string vol = FormatFloat2(0.0);
    SetItemValueLogged(edit, jobdir, obj, L"映像再生", L"音量", vol.c_str(), L"hide");

    // restore cursor
    edit->set_cursor_layer_frame(old_layer, old_frame);
}

// Safe iteration helper for edit->find_object():
//  - Some environments may return the same object again if the search frame
//    does not advance "past" the current object.
//  - To avoid infinite loops, we ensure the next search frame strictly increases.
// ------------------------------------------------------------
static OBJECT_HANDLE FindNextObjectSafe(EDIT_SECTION* edit, int layer, OBJECT_HANDLE cur, int& search_frame) {
    if (!edit || !cur) return nullptr;
    OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(cur);

    // try with lf.end first
    int next_frame = lf.end;
    if (next_frame <= search_frame) next_frame = search_frame + 1;

    OBJECT_HANDLE nxt = edit->find_object(layer, next_frame);
    if (nxt == cur) {
        // bump by +1 if the same object is returned
        if (next_frame < (std::numeric_limits<int>::max)()) {
            next_frame += 1;
        }
        nxt = edit->find_object(layer, next_frame);
    }

    // still not advancing -> force advance
    if (nxt == cur) {
        AppendLog(fs::path{}, L"[warn] find_object did not advance; forcing +1 frame.");
        if (next_frame < (std::numeric_limits<int>::max)()) {
            next_frame += 1;
        }
        nxt = edit->find_object(layer, next_frame);
    }

    search_frame = next_frame;
    return nxt;
}


static OBJECT_HANDLE FindObjectCoveringFrame(EDIT_SECTION* edit, int layer, int frame) {
	if (!edit) return nullptr;
    int search_frame = 0;
    OBJECT_HANDLE h = edit->find_object(layer, search_frame);
    int guard = 0;
    while (h) {
        if (++guard > 200000) {
            AppendLog(fs::path{}, L"[error] FindObjectCoveringFrame: loop guard hit (possible infinite loop).");
            return nullptr;
        }
		OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(h);
		if (lf.end <= frame) {
		    h = FindNextObjectSafe(edit, layer, h, search_frame);
			continue;
		}
		if (lf.start > frame) break;
		// frame is in [lf.start, lf.end)
		return h;
	}
	return nullptr;
}

// Try to identify the exact object by alias match (safer if user moved objects),
// fallback to "covering frame" if alias changed by edits.
static OBJECT_HANDLE FindObjectCoveringFrameByExactAlias(
	EDIT_SECTION* edit, int layer, int frame, const std::string& alias_utf8
) {
	if (!edit) return nullptr;
	if (alias_utf8.empty()) return FindObjectCoveringFrame(edit, layer, frame);

    int search_frame = 0;
    OBJECT_HANDLE h = edit->find_object(layer, search_frame);
    OBJECT_HANDLE fallback = nullptr;
    int guard = 0;
    while (h) {
        if (++guard > 200000) {
            AppendLog(fs::path{}, L"[error] FindObjectCoveringFrameByExactAlias: loop guard hit (possible infinite loop).");
            return fallback;
        }
		OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(h);
		if (lf.end <= frame) {
			h = FindNextObjectSafe(edit, layer, h, search_frame);
			continue;
		}
		if (lf.start > frame) break;
		// frame is in [lf.start, lf.end)
		const char* a = edit->get_object_alias(h);
        if (a && alias_utf8 == std::string(a)) return h;
        // keep as fallback, but keep scanning in case of overlaps and an exact match exists
        if (!fallback) fallback = h;
        h = FindNextObjectSafe(edit, layer, h, search_frame);
	}
	return fallback;
}

struct ApplyCtx {
    std::string alias_utf8;
    int frame = 0;
    int base_layer = 0;        // 元のレイヤ（ここから上を探す）
    int fallback_length = 1;   // フォールバックの長さ（フレーム数）
    int out_num_frames = 0;    // result.json の num_frames（無ければ 0）
    double out_fps = 0.0;      // result.json の fps（無ければ 0）

    int used_layer = -1;       // 実際に使った layer（デバッグ用）
    int used_length = 0;       // 実際に使った length（デバッグ用）
    bool created = false;
    OBJECT_HANDLE created_obj = nullptr;
    std::wstring name_w;
    bool focus_after = true;

	// auto-hide (do not delete) source object after successful insert
	bool hide_source = true;
	int  src_layer = -1;
	int  src_frame = -1;            // any frame inside the source object's range (usually start_frame)
	std::string src_alias_utf8;     // original (unpatched) alias for identification
    fs::path log_dir;
};

static ApplyCtx* g_apply_ctx = nullptr;
static void __cdecl ApplyCreateProcImpl(EDIT_SECTION* edit);
static void LogApplySehException(const fs::path* jobdir, DWORD code);
static bool IsLayerRangeFree(EDIT_SECTION* edit, int layer, int start_frame, int end_frame_excl);
static int  FindFreeLayerAbove(EDIT_SECTION* edit, int base_layer, int start_frame, int end_frame_excl);

// SEH wrapper: MUST NOT create any C++ objects with destructors in this function,
// otherwise MSVC errors with C2712.
static void __cdecl ApplyCreateProc(EDIT_SECTION* edit) {
    if (!edit || !g_apply_ctx) return;

    const fs::path* jobdir = &g_apply_ctx->log_dir; // pointer is POD (no destructor)
    DWORD seh_code = 0;
    int   seh_hit = 0;

    __try {
        ApplyCreateProcImpl(edit);
    }
    __except ((seh_code = GetExceptionCode(), seh_hit = 1, EXCEPTION_EXECUTE_HANDLER)) {
        // do nothing here (no C++ objects!)
    }
    if (seh_hit) {
        LogApplySehException(jobdir, seh_code);
    }
}

static void LogApplySehException(const fs::path* jobdir, DWORD code) {
    if (!jobdir) return;
    AppendLog(*jobdir, L"[apply] SEH exception in ApplyCreateProc: " + HexU32W(code));
}

// Real implementation (C++ objects are OK here; no __try / __except).
static void __cdecl ApplyCreateProcImpl(EDIT_SECTION* edit) {
    if (!edit || !g_apply_ctx) return;

    const fs::path& jobdir = g_apply_ctx->log_dir;

    if (edit->info) {
        AppendLog(jobdir, L"[apply] edit->info cursor layer=" + std::to_wstring(edit->info->layer) +
            L" frame=" + std::to_wstring(edit->info->frame));
    }

    int frame = g_apply_ctx->frame;
    int length = g_apply_ctx->fallback_length;
    if (length < 1) length = 1;

    // project fps は EDIT_SECTION 内で取る
    const double proj_fps = GetProjectFps(edit);

    // result.json の num_frames/fps があるなら length を換算
    if (g_apply_ctx->out_num_frames > 0) {
        if (g_apply_ctx->out_fps > 0.0 && proj_fps > 0.0) {
            double scaled = (double)g_apply_ctx->out_num_frames * (proj_fps / g_apply_ctx->out_fps);
            length = (int)std::llround(scaled);
        } else {
            length = g_apply_ctx->out_num_frames;
        }
        if (length < 1) length = 1;
    }

    int end_frame_excl = frame + length;
    int base_layer = g_apply_ctx->base_layer;
    if (base_layer < 0) base_layer = 0;

    // 空きレイヤ探索も EDIT_SECTION 内で
    int layer = FindFreeLayerAbove(edit, base_layer, frame, end_frame_excl);

    if (layer < 0) layer = 0;
    if (layer > 9999) layer = 9999; // あなたの FindFreeLayerAbove と整合
    if (frame < 0) frame = 0;
    if (length < 1) length = 1;

    AppendLog(jobdir,
        L"[apply] about to call create_object_from_alias"
        L" layer=" + std::to_wstring(layer) +
        L" frame=" + std::to_wstring(frame) +
        L" length=" + std::to_wstring(length) +
        L" (note: alias frame info may override length)");

    OBJECT_HANDLE obj = edit->create_object_from_alias(
        g_apply_ctx->alias_utf8.c_str(),
        layer, frame, length
    );
    AppendLog(jobdir, L"[apply] create_object_from_alias => " + std::wstring(obj ? L"non-null" : L"null"));
    if (obj) LogObjectRange(edit, jobdir, L"apply.created", obj);

    g_apply_ctx->created = (obj != nullptr);
    g_apply_ctx->created_obj = obj;
    g_apply_ctx->used_layer = layer;
    g_apply_ctx->used_length = length;
    if (obj) {
        if (!g_apply_ctx->name_w.empty()) {
            edit->set_object_name(obj, g_apply_ctx->name_w.c_str());
        }
        if (g_apply_ctx->focus_after) {
            edit->set_focus_object(obj);
        }
    }

    // After successful creation, hide source object without deleting it
    // (set opacity=100 + audio disable)
    if (obj && g_apply_ctx->hide_source) {
        OBJECT_HANDLE src = FindObjectCoveringFrameByExactAlias(
            edit,
            (g_apply_ctx->src_layer >= 0) ? g_apply_ctx->src_layer : g_apply_ctx->base_layer,
            (g_apply_ctx->src_frame >= 0) ? g_apply_ctx->src_frame : g_apply_ctx->frame,
            g_apply_ctx->src_alias_utf8
        );
        AppendLog(jobdir, std::wstring(L"[hide] FindObjectCoveringFrameByExactAlias => ") + (src ? L"found" : L"not found"));
        if (src) {
            LogObjectRange(edit, jobdir, L"hide.src", src);
            // write before-values
            LogItemValue(edit, jobdir, src, L"映像再生", L"透明度", L"hide.before");
            LogItemValue(edit, jobdir, src, L"動画ファイル", L"音声付き", L"hide.before");
            LogItemValue(edit, jobdir, src, L"映像再生", L"音量", L"hide.before");

            HideObjectByOpacityAndMute(edit, jobdir, src, (g_apply_ctx->src_frame >= 0) ? g_apply_ctx->src_frame : g_apply_ctx->frame);

            // write after-values
            LogItemValue(edit, jobdir, src, L"映像再生", L"透明度", L"hide.after");
            LogItemValue(edit, jobdir, src, L"動画ファイル", L"音声付き", L"hide.after");
            LogItemValue(edit, jobdir, src, L"映像再生", L"音量", L"hide.after");
        }
    }
}

// [start, end_excl) に、指定 layer 上のオブジェクトが重なるか？
// find_object は「指定 frame 上、または以降」のオブジェクトを返す仕様。 :contentReference[oaicite:4]{index=4}
static bool IsLayerRangeFree(EDIT_SECTION* edit, int layer, int start_frame, int end_frame_excl) {
    if (!edit) return true;
    // find_object は「start>=frame」の検索なので、必ず先頭から走査して重なりを判定する:contentReference[oaicite:3]{index=3}
    int search_frame = 0;
    OBJECT_HANDLE h = edit->find_object(layer, search_frame);
    int guard = 0;
    while (h) {
        if (++guard > 500000) {
            AppendLog(fs::path{}, L"[error] IsLayerRangeFree: loop guard hit (possible infinite loop). Returning false.");
            return false;
        }
        OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(h);
        if (lf.end <= start_frame) {
            // 次へ（進めるために lf.end を使う）
            h = FindNextObjectSafe(edit, layer, h, search_frame);
            continue;
        }
        if (lf.start >= end_frame_excl) break; // 以降は範囲外
        return false; // overlap
    }
    return true;
}

static int FindFreeLayerAbove(EDIT_SECTION* edit, int base_layer, int start_frame, int end_frame_excl) {
    if (!edit || !edit->info) return base_layer + 1;
    // 適当に上限に余裕を持たせる（layer_max は存在する最大レイヤ）
    int max_try = std::max(edit->info->layer_max + 16, base_layer + 32);
    // 念のため上限を抑える
    max_try = std::min(max_try, 9999);
    
    // debug (plugin-level log only)
    AppendLog(fs::path{}, L"[layer] FindFreeLayerAbove base=" + std::to_wstring(base_layer) +
        L" start=" + std::to_wstring(start_frame) +
        L" end_excl=" + std::to_wstring(end_frame_excl) +
        L" max_try=" + std::to_wstring(max_try));
    for (int layer = base_layer + 1; layer <= max_try; ++layer) {
        if (IsLayerRangeFree(edit, layer, start_frame, end_frame_excl)) {
            AppendLog(fs::path{}, L"[layer] free layer found=" + std::to_wstring(layer));
            return layer;
        }
    }
    AppendLog(fs::path{}, L"[layer] no free layer found; fallback=" + std::to_wstring(base_layer + 1));
    return base_layer + 1; // 最後の手段（ここで create が失敗したらログに出す）
}


// 超簡易 JSON 文字列抽出: "key":"value" を拾うだけ（Phase1-3 用）
static std::optional<std::string> ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t kpos = json.find(pat);
    if (kpos == std::string::npos) return std::nullopt;

    size_t colon = json.find(':', kpos + pat.size());
    if (colon == std::string::npos) return std::nullopt;

    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) i++;

    if (i >= json.size() || json[i] != '"') return std::nullopt;
    i++; // skip first quote

    std::string out;
    out.reserve(64);
    for (; i < json.size(); i++) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) { // minimal unescape
            char n = json[++i];
            if (n == '"' || n == '\\' || n == '/') out.push_back(n);
            else if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else out.push_back(n);
        } else if (c == '"') {
            return out;
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

// ---- JSON minimal helpers (string/bool/number) ----
static inline void SkipJsonWs(const std::string& s, size_t& i) {
    while (i < s.size()) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { i++; continue; }
        break;
    }
}

static std::optional<std::string> ExtractJsonString2(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t kpos = json.find(pat);
    if (kpos == std::string::npos) return std::nullopt;

    size_t colon = json.find(':', kpos + pat.size());
    if (colon == std::string::npos) return std::nullopt;

    size_t i = colon + 1;
    SkipJsonWs(json, i);
    if (i >= json.size() || json[i] != '"') return std::nullopt;
    i++;

    std::string out;
    out.reserve(64);
    for (; i < json.size(); i++) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            char n = json[++i];
            if (n == '"' || n == '\\' || n == '/') out.push_back(n);
            else if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else out.push_back(n);
        } else if (c == '"') {
            return out;
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

static std::optional<bool> ExtractJsonBool(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t kpos = json.find(pat);
    if (kpos == std::string::npos) return std::nullopt;

    size_t colon = json.find(':', kpos + pat.size());
    if (colon == std::string::npos) return std::nullopt;

    size_t i = colon + 1;
    SkipJsonWs(json, i);
    if (i + 4 <= json.size() && json.compare(i, 4, "true") == 0) return true;
    if (i + 5 <= json.size() && json.compare(i, 5, "false") == 0) return false;
    return std::nullopt;
}

static std::optional<double> ExtractJsonNumber(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t kpos = json.find(pat);
    if (kpos == std::string::npos) return std::nullopt;

    size_t colon = json.find(':', kpos + pat.size());
    if (colon == std::string::npos) return std::nullopt;

    size_t i = colon + 1;
    SkipJsonWs(json, i);
    if (i >= json.size()) return std::nullopt;

    // parse number token
    size_t j = i;
    while (j < json.size()) {
        char c = json[j];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') { j++; continue; }
        break;
    }
    if (j == i) return std::nullopt;

    std::string tok = json.substr(i, j - i);
    char* endp = nullptr;
    double v = std::strtod(tok.c_str(), &endp);
    if (endp == tok.c_str()) return std::nullopt;
    return v;
}


// ------------------------------------------------------------
// log helpers (write ONLY to jobdir)
// ------------------------------------------------------------
static std::wstring NowStampW() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64]{};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}


static void AppendLogOne(const fs::path& dir, const fs::path& filename, const std::wstring& msg) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path logp = dir / filename;
    std::ofstream ofs(logp, std::ios::app | std::ios::binary);
    if (!ofs) return;
    std::wstring linew = NowStampW() + L" [tid=" + std::to_wstring(GetCurrentThreadId()) + L"] " + msg;
    std::string line = WideToUtf8(linew);
    ofs.write(line.data(), (std::streamsize)line.size());
    ofs.write("\r\n", 2);
}

static void AppendLog(const fs::path& jobdir, const std::wstring& msg) {
    // Write ONLY when jobdir is available.
    if (jobdir.empty()) return;
    AppendLogOne(jobdir, L"launcher.log.txt", msg);
}

static void LogObjectRange(EDIT_SECTION* edit, const fs::path& jobdir, const wchar_t* tag, OBJECT_HANDLE obj) {
    if (!edit || !obj) {
        AppendLog(jobdir, std::wstring(L"[") + tag + L"] obj=null");
        return;
    }
    OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(obj);
    AppendLog(jobdir,
        std::wstring(L"[") + tag + L"] layer=" + std::to_wstring(lf.layer) +
        L" start=" + std::to_wstring(lf.start) +
        L" end=" + std::to_wstring(lf.end) + L"(exclusive)");
}


static std::wstring WinErrorMessage(DWORD err) {
    wchar_t* buf = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&buf, 0, nullptr
    );
    std::wstring s = buf ? buf : L"";
    if (buf) LocalFree(buf);
    return s;
}

static HANDLE   g_child_proc = nullptr;
static HANDLE   g_child_job  = nullptr;
static UINT_PTR g_timer_id   = 0;
static bool     g_job_done   = false;

// ------------------------------------------------------------
// Child process (python) lifecycle helpers
//  - We assume: python will exit by itself after writing result.json (success true/false)
//  - We still clean up handles, and if it doesn't exit, we can force-kill.
// ------------------------------------------------------------
static void CloseHandleSafe(HANDLE& h) {
    if (h) {
        CloseHandle(h);
        h = nullptr;
    }
}
static bool IsChildProcessRunning(DWORD* out_exit_code = nullptr) {
    if (!g_child_proc) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(g_child_proc, &code)) return false;
    if (out_exit_code) *out_exit_code = code;
    return (code == STILL_ACTIVE);
}

static void StopPollingTimer(HWND hwnd) {
    if (g_timer_id) {
        KillTimer(hwnd, g_timer_id);
        g_timer_id = 0;
    }
}

// Force kill: kills process tree if JobObject is available; otherwise kills the process only.
static void ForceKillChildProcessTree(const fs::path& jobdir, const wchar_t* why) {
    if (!g_child_proc && !g_child_job) return;

    AppendLog(jobdir, std::wstring(L"[proc] force-kill python: ") + (why ? why : L"(no reason)"));

    if (g_child_job) {
        // If process was assigned to this job, this kills its children too.
        TerminateJobObject(g_child_job, 1);
    } else if (g_child_proc) {
        TerminateProcess(g_child_proc, 1);
    }

    if (g_child_proc) {
        WaitForSingleObject(g_child_proc, 2000);
    }

    CloseHandleSafe(g_child_proc);
    CloseHandleSafe(g_child_job);
}
// Wait a bit for natural exit (expected), then cleanup handles.
static void CleanupChildProcessAfterJobDone(const fs::path& jobdir) {
    if (!g_child_proc && !g_child_job) return;

    // If python already exited, just close handles.
    if (g_child_proc) {
        DWORD w = WaitForSingleObject(g_child_proc, 2000); // short grace period
        if (w == WAIT_TIMEOUT) {
            // Unexpected: python should have exited after writing result.json
            ForceKillChildProcessTree(jobdir, L"timeout waiting for python to exit after result.json");
            return;
        }
    }

    AppendLog(jobdir, L"[proc] python exited; closing handles.");
    CloseHandleSafe(g_child_proc);
    CloseHandleSafe(g_child_job);
}

static void LogItemValue(EDIT_SECTION* edit, const fs::path& jobdir, OBJECT_HANDLE obj,
    const wchar_t* eff, const wchar_t* item, const wchar_t* tag)
{
    if (!edit || !obj) return;
    const char* v = edit->get_object_item_value(obj, eff, item);
    std::wstring wv = v ? Utf8ToWide(v) : L"(null)";
    AppendLog(jobdir, std::wstring(L"[") + tag + L"] get_object_item_value [" + eff + L"][" + item + L"]=" + wv);
}

static bool SetItemValueLogged(EDIT_SECTION* edit, const fs::path& jobdir, OBJECT_HANDLE obj,
    const wchar_t* eff, const wchar_t* item, const char* value, const wchar_t* tag)
{
    if (!edit || !obj) return false;
    bool ok = edit->set_object_item_value(obj, eff, item, value);
    AppendLog(jobdir,
        std::wstring(L"[") + tag + L"] set_object_item_value [" + eff + L"][" + item + L"]=" +
        Utf8ToWide(value) + L" => " + (ok ? L"true" : L"false"));
    LogItemValue(edit, jobdir, obj, eff, item, tag);
    return ok;
}

// ---- 取得したい “フォーカス動画” 情報 ----
struct FocusVideoInfo {
    bool ok = false;
    std::wstring source_video_path; // wide path
    int layer = -1;
    int start_frame = -1;
    int end_frame = -1;
    double timeline_fps = 30.0;

    double playback_start_sec = 0.0;
    double playback_end_sec = 0.0;

    std::string alias_utf8; // デバッグ用
    std::wstring note;
};

// ---- グローバル ----
static HOST_APP_TABLE* g_host = nullptr;
static EDIT_HANDLE* g_edit = nullptr;

static FocusVideoInfo CaptureFocusVideo(EDIT_SECTION* edit); 
static FocusVideoInfo* g_capture_dst = nullptr;

static void __cdecl CaptureProc(EDIT_SECTION* edit) {
    if (!g_capture_dst) return;
    *g_capture_dst = CaptureFocusVideo(edit);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hmod = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        g_hmod = nullptr;
        break;
    }
    return TRUE;
}

static HWND g_hwnd = nullptr;
static HWND g_btn_capture = nullptr;
static HWND g_btn_run = nullptr;
static HWND g_btn_open = nullptr;
static HWND g_edit_path = nullptr;
static HWND g_edit_job = nullptr;
static HWND g_static_status = nullptr;
static HWND g_combo_insert = nullptr;
static HFONT g_ui_font = nullptr;

static HFONT CreateSystemMessageFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
        // Windows の標準UIフォント（日本語環境なら Yu Gothic UI など）を使う
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    // フォールバック（Segoe UI / MS Shell Dlg 2 相当になりやすい）
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}
static BOOL CALLBACK EnumChildSetFontProc(HWND child, LPARAM lp) {
    HFONT hf = (HFONT)lp;
    SendMessageW(child, WM_SETFONT, (WPARAM)hf, (LPARAM)TRUE);
    return TRUE;
}

static void ApplyUiFont(HWND hwnd) {
    if (!g_ui_font) g_ui_font = CreateSystemMessageFont();
    if (!g_ui_font) return;
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_ui_font, (LPARAM)FALSE);
    EnumChildWindows(hwnd, EnumChildSetFontProc, (LPARAM)g_ui_font);
}

static FocusVideoInfo g_last_focus{};
static fs::path g_last_job_dir{};

static fs::path PluginRootDir() {
    fs::path dir = fs::path(GetModuleDirW(g_hmod)); // DLLがあるフォルダ
    fs::path cand = dir / L"SAM3";
    if (fs::exists(cand) && fs::is_directory(cand)) return cand;
    return dir;
}

static fs::path JobsRootDir() {
    return PluginRootDir() / L"Jobs";
}

static std::string MakeJobId() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    DWORD pid = GetCurrentProcessId();
    // YYYYMMDD_HHMMSS_pid
    char buf[64];
    sprintf_s(buf, "%04d%02d%02d_%02d%02d%02d_%lu",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (unsigned long)pid);
    return std::string(buf);
}

static void JobsGCKeepLatestN(int keepN = 20) {
    std::error_code ec;
    fs::create_directories(JobsRootDir(), ec);

    std::vector<fs::directory_entry> dirs;
    for (auto& it : fs::directory_iterator(JobsRootDir(), ec)) {
        if (ec) break;
        if (it.is_directory()) dirs.push_back(it);
    }
    if ((int)dirs.size() <= keepN) return;

    // job_id が時刻先頭なので辞書順で古い→新しいになる想定
    std::sort(dirs.begin(), dirs.end(), [](auto& a, auto& b) {
        return a.path().filename().wstring() < b.path().filename().wstring();
    });

    int removeN = (int)dirs.size() - keepN;
    for (int i = 0; i < removeN; i++) {
        fs::remove_all(dirs[i].path(), ec);
        ec.clear();
    }
}

// ---- EDIT_SECTION 内でフォーカスを取る ----
static FocusVideoInfo CaptureFocusVideo(EDIT_SECTION* edit) {
    FocusVideoInfo out;
    if (!edit) { out.note = L"edit section is null"; return out; }

    // フォーカスオブジェクト
    OBJECT_HANDLE obj = edit->get_focus_object();
    if (!obj) {
        int n = edit->get_selected_object_num();
        if (n > 0) obj = edit->get_selected_object(0);
    }
    if (!obj) { out.note = L"No focus/selected object."; return out; }

    OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(obj);
    out.layer = lf.layer;
    out.start_frame = lf.start;
    out.end_frame = (lf.end > lf.start) ? (lf.end - 1) : lf.start;

    // fps（タイムライン換算フォールバック用）
    double fps = 30.0;
    if (edit->info && edit->info->scale != 0) {
        fps = (double)edit->info->rate / (double)edit->info->scale;
    }
    if (fps <= 0.0) fps = 30.0;
    out.timeline_fps = fps;

    // フォールバック：タイムラインのフレーム範囲を秒にしたもの
    out.playback_start_sec = (double)lf.start / fps;
    out.playback_end_sec   = (double)lf.end   / fps; // endはexclusive想定

    // alias（UTF-8）
    if (auto alias = edit->get_object_alias(obj)) {
        out.alias_utf8 = alias;
    }

    // 1) alias解析：動画ファイル効果の「ファイル=」「再生位置=」を優先
    bool alias_file_ok = false;
    bool alias_play_ok = false;
    if (!out.alias_utf8.empty()) {
        AliasVideoInfo a = ParseVideoInfoFromAliasUtf8(out.alias_utf8);

        if (a.has_file && !a.file_utf8.empty()) {
            out.source_video_path = Utf8ToWide(a.file_utf8);
            alias_file_ok = true;
        }

        // 再生位置は「開始<終了」のときだけ採用（0,0 などは無視）
        if (a.has_playback && a.playback_end > a.playback_start) {
            out.playback_start_sec = a.playback_start;
            out.playback_end_sec   = a.playback_end;
            alias_play_ok = true;
        }
    }

    // 2) aliasで取れなかった場合だけ、従来の item 参照を試す（フォールバック）
    if (!alias_file_ok) {
        const wchar_t* effects[] = { L"動画ファイル", L"Video File", L"動画", L"ビデオファイル" };
        const wchar_t* items[]   = { L"ファイル", L"file", L"path", L"File", L"Path" };

        std::string fileUtf8;
        for (auto eff : effects) {
            for (auto item : items) {
                const char* v = edit->get_object_item_value(obj, eff, item);
                if (v && v[0]) { fileUtf8 = v; break; }
            }
            if (!fileUtf8.empty()) break;
        }

        if (!fileUtf8.empty()) {
            out.source_video_path = Utf8ToWide(fileUtf8);
        }
    }

    // note（デバッグ用）
    {
        std::wstring note;
        if (alias_file_ok) note += L"[alias:file OK] ";
        else               note += L"[alias:file NG] ";

        if (alias_play_ok) note += L"[alias:play OK] ";
        else               note += L"[alias:play NG] ";

        if (out.source_video_path.empty()) note += L"[source empty] ";
        out.note = note;
    }

    out.ok = !out.source_video_path.empty();
    if (!out.ok) {
        out.note += L"[capture failed: source_video_path empty]";
    }
    return out;
}



// ---- request.json を作る ----
static std::string BuildRequestJsonV1(
    const std::string& job_id,
    const FocusVideoInfo& f,
    const fs::path& output_dir_utf16, // output_dir は JSON では UTF-8
    const std::string& bg_mode,
    const std::string& insert_mode
) {
    // Phase1-3: prompt/output は固定（click + mask）でまず通す
    std::string source_path_utf8 = WideToUtf8(f.source_video_path);
    std::string output_dir_utf8 = WideToUtf8(output_dir_utf16.wstring());

    // もし source_path が空なら空のまま出す（Python 側で UI から指定できるようにする余地）
    std::ostringstream ss;
    ss
        << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"job_id\": \"" << JsonEscape(job_id) << "\",\n"
        << "\n"
        << "  \"source_video_path\": \"" << JsonEscape(source_path_utf8) << "\",\n"
        << "  \"playback_start_sec\": " << f.playback_start_sec << ",\n"
        << "  \"playback_end_sec\": " << f.playback_end_sec << ",\n"
        << "\n"
        << "  \"timeline\": {\n"
        << "    \"layer\": " << f.layer << ",\n"
        << "    \"start_frame\": " << f.start_frame << ",\n"
        << "    \"end_frame\": " << f.end_frame << ",\n"
        << "    \"fps\": " << f.timeline_fps << "\n"
        << "  },\n"
        << "\n"
        << "  \"snapshot\": {\n"
        << "    \"focus_object_alias_hash\": \"\",\n"
        << "    \"note\": \"Mismatch allowed; apply may be skipped.\"\n"
        << "  },\n"
        << "\n"
        << "  \"prompt\": {\n"
        << "    \"mode\": \"click\",\n"
        << "    \"text\": \"\",\n"
        << "    \"language\": \"ja\",\n"
        << "    \"instance_policy\": \"union_all\"\n"
        << "  },\n"
        << "\n"
        << "  \"output\": {\n"
        << "    \"mode\": \"fgmask\",\n"
        << "    \"insert_mode\": \"" << JsonEscape(insert_mode) << "\",\n"
        << "    \"bg_mode\": \"" << JsonEscape(bg_mode) << "\",\n"
        << "    \"apply_policy\": {\n"
        << "      \"timeline_apply\": \"duplicate\",\n"
        << "      \"target_object\": \"focus\",\n"
        << "      \"layer_policy\": \"orig_plus_1_then_down\"\n"
        << "    },\n"
        << "    \"mask\": {\n"
        << "      \"file_format\": \"mp4\",\n"
        << "      \"codec\": \"mp4v\",\n"
        << "      \"value_range\": \"0_255\",\n"
        << "      \"prefer_soft_mask\": true,\n"
        << "      \"module_name\": \"SAM3mask\"\n"
        << "    },\n"
        << "    \"gbbb\": {\n"
        << "      \"codec\": \"mp4v\",\n"
        << "      \"audio_mux\": true\n"
        << "    }\n"
        << "  },\n"
        << "\n"
        << "  \"output_dir\": \"" << JsonEscape(output_dir_utf8) << "\",\n"
        << "  \"timestamp_jst\": \"\",\n"
        << "\n"
        << "  \"options\": {\n"
        << "    \"max_seconds\": 0,\n"
        << "    \"device_preference\": \"cuda\",\n"
        << "    \"decode_mode\": \"segment_only\",\n"
        << "    \"write_atomic\": true,\n"
        << "    \"gradio\": {\n"
        << "      \"host\": \"127.0.0.1\",\n"
        << "      \"port\": 0\n"
        << "    }\n"
        << "  }\n"
        << "}\n";
    return ss.str();
}

static void UpdateUiText(HWND hwnd, const FocusVideoInfo& f, const fs::path& jobdir, const std::wstring& status) {
    if (g_edit_path) {
        SetWindowTextW(g_edit_path, f.source_video_path.empty() ? L"(source_video_path unresolved)" : f.source_video_path.c_str());
    }
    if (g_edit_job) {
        SetWindowTextW(g_edit_job, jobdir.empty() ? L"" : jobdir.wstring().c_str());
    }
    if (g_static_status) {
        SetWindowTextW(g_static_status, status.c_str());
    }
}

// ---- Python 起動 ----
static std::optional<std::wstring> FindPythonExe() {
    // 明示的に環境変数に上書き
    wchar_t envbuf[32768]{};
    DWORD n = GetEnvironmentVariableW(L"SAM3_PYTHON_EXE", envbuf, (DWORD)std::size(envbuf));
    if (n > 0 && n < (DWORD)std::size(envbuf)) {
        fs::path p = fs::path(envbuf);
        std::error_code ec;
        if (fs::exists(p, ec) && !fs::is_directory(p, ec)) {
            return p.wstring();
        }
    }

    // Pythonの場所
    fs::path root = PluginRootDir();
    const fs::path cands[] = {
        root / L"Python" / L".venv" / L"Scripts" / L"python.exe",
        root / L"python" / L".venv" / L"Scripts" / L"python.exe",
        root / L"Python" / L"python.exe",
        root / L"python" / L"python.exe",
    };
    for (const auto& p : cands) {
        std::error_code ec;
        if (fs::exists(p, ec) && !fs::is_directory(p, ec)) {
            return p.wstring();
        }
    }

    // PATHを検索
    wchar_t pathbuf[MAX_PATH * 4]{};
    DWORD got = SearchPathW(nullptr, L"python.exe", nullptr, (DWORD)std::size(pathbuf), pathbuf, nullptr);
    if (got > 0 && got < (DWORD)std::size(pathbuf)) {
        return std::wstring(pathbuf);
    }

    return std::nullopt;
}

static fs::path PythonScriptPath() {
    fs::path root = PluginRootDir();
    fs::path p1 = root / L"Python" / L"sam3_gradio_job.py";
    fs::path p2 = root / L"python" / L"sam3_gradio_job.py";
    if (fs::exists(p1)) return p1;
    if (fs::exists(p2)) return p2;
    return p1;
}

static bool LaunchPythonJob(const fs::path& jobdir) {
    AppendLog(jobdir, L"=== LaunchPythonJob begin ===");
    AppendLog(jobdir, L"ModulePath: " + GetModulePathW(g_hmod));
    AppendLog(jobdir, L"PluginRootDir: " + PluginRootDir().wstring());

    if (g_child_proc || g_child_job) ForceKillChildProcessTree(jobdir, L"pre-launch cleanup");

    auto py = FindPythonExe();
    if (!py) {
        AppendLog(jobdir, L"FindPythonExe FAILED. Expected one of:");
        AppendLog(jobdir, (PluginRootDir() / L"Python" / L".venv" / L"Scripts" / L"python.exe").wstring());
        AppendLog(jobdir, (PluginRootDir() / L"Python" / L"python.exe").wstring());
        return false;
    }
    AppendLog(jobdir, L"PythonExe: " + *py);

    fs::path script = PythonScriptPath();
    AppendLog(jobdir, L"ScriptPath: " + script.wstring());
    if (!fs::exists(script)) {
        AppendLog(jobdir, L"Script NOT FOUND.");
        return false;
    }

    fs::path wdir = PluginRootDir() / L"Python";
    AppendLog(jobdir, L"WorkDir: " + wdir.wstring());

    std::wstring cmd =
         L"\"" + *py + L"\" -u \"" + script.wstring() + L"\" --job_dir \"" + jobdir.wstring() + L"\"";
    AppendLog(jobdir, L"CmdLine: " + cmd);

    // Redirect python stdout/stderr to files under jobdir
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    fs::path outp = jobdir / L"python.stdout.txt";
    fs::path errp = jobdir / L"python.stderr.txt";
    HANDLE hOut = CreateFileW(outp.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE hErr = CreateFileW(errp.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) hOut = nullptr;
    if (hErr == INVALID_HANDLE_VALUE) hErr = nullptr;
    AppendLog(jobdir, L"python stdout: " + outp.wstring());
    AppendLog(jobdir, L"python stderr: " + errp.wstring());

    // CreateProcessW用に「書き換え可能なバッファ」を作る（安全策）
    std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hOut ? hOut : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = hErr ? hErr : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};

    // Create JobObject to be able to kill python's process tree if needed.
    // NOTE: AssignProcessToJobObject can fail if the host process is already inside a job
    // that disallows breakaway. In that case, we still proceed without job control.
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            DWORD e = GetLastError();
            AppendLog(jobdir, L"[proc] SetInformationJobObject failed: " + std::to_wstring(e) + L" " + WinErrorMessage(e));
            CloseHandle(hJob);
            hJob = nullptr;
        }
    } else {
        DWORD e = GetLastError();
        AppendLog(jobdir, L"[proc] CreateJobObjectW failed: " + std::to_wstring(e) + L" " + WinErrorMessage(e));
    }

    BOOL ok = CreateProcessW(
        nullptr,
        cmdline.data(),
        nullptr, nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        wdir.wstring().c_str(),
        &si, &pi
    );

    if (hOut) CloseHandle(hOut);
    if (hErr) CloseHandle(hErr);

    if (!ok) {
        DWORD err = GetLastError();
        AppendLog(jobdir, L"CreateProcessW FAILED. GetLastError=" + std::to_wstring(err));
        AppendLog(jobdir, L"Message: " + WinErrorMessage(err));
        if (hJob) CloseHandle(hJob);
        return false;
    }

    AppendLog(jobdir, L"CreateProcessW OK.");

    // Try to assign to JobObject (optional).
    if (hJob) {
        if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
            DWORD e = GetLastError();
            AppendLog(jobdir, L"[proc] AssignProcessToJobObject failed: " + std::to_wstring(e) + L" " + WinErrorMessage(e));
            CloseHandle(hJob);
            hJob = nullptr;
        } else {
            AppendLog(jobdir, L"[proc] Assigned python process to JobObject (tree-kill enabled).");
        }
    }

    CloseHandleSafe(g_child_proc);
    CloseHandleSafe(g_child_job);
    g_child_proc = pi.hProcess;
    g_child_job  = hJob;
    CloseHandle(pi.hThread);
    return true;
}

static fs::path GetJobDirFromUiOrLast() {
    // UIの jobdir 表示を優先（g_last_job_dir とズレても確実）
    if (g_edit_job) {
        wchar_t buf[2048]{};
        GetWindowTextW(g_edit_job, buf, (int)std::size(buf));
        if (buf[0]) return fs::path(buf);
    }
    return g_last_job_dir;
}

static std::optional<std::wstring> ReadGradioUrlWithRetry(const fs::path& jobdir, int retry = 40, int sleep_ms = 50) {
    fs::path status = jobdir / L"status.json";

    for (int i = 0; i < retry; ++i) {
        if (auto s = ReadAllText(status)) {
            if (auto url = ExtractJsonString(*s, "gradio_url")) {
                if (!url->empty()) {
                    return Utf8ToWide(*url);
                }
                // url が空なら “まだ準備中” とみなしてリトライ継続
            }
            // key が見つからない/パース失敗でもリトライ継続
        }
        Sleep(sleep_ms);
    }
    return std::nullopt;
}


// ---- status/result ポーリング ----
static void PollJobFiles() {
    if (g_last_job_dir.empty()) return;

    fs::path statusp = g_last_job_dir / L"status.json";
    fs::path resultp = g_last_job_dir / L"result.json";

    std::wstring ui = L"";

    // If python already exited unexpectedly and there is no result.json, stop polling and report.
    if (g_child_proc) {
        DWORD ec = 0;
        if (!IsChildProcessRunning(&ec)) {
            if (!fs::exists(resultp)) {
                ui += L"[poll] Python process exited before result.json was created.\n";
                ui += L"exit_code=" + std::to_wstring(ec) + L"\n";
                ui += L"check: python.stdout.txt / python.stderr.txt";
                UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);
                StopPollingTimer(g_hwnd);
                CleanupChildProcessAfterJobDone(g_last_job_dir);
                return;
            }
            // result.json exists => expected path; we'll handle it below and cleanup then.
        }
    }

    // ---- status.json ----
    if (auto s = ReadAllText(statusp)) {
        auto st = ExtractJsonString2(*s, "state");
        auto ph = ExtractJsonString2(*s, "phase");
        auto msg = ExtractJsonString2(*s, "message");
        auto url = ExtractJsonString2(*s, "gradio_url");
        auto prog = ExtractJsonNumber(*s, "progress");

        ui += L"state=" + (st ? Utf8ToWide(*st) : L"(?)");
        ui += L"  phase=" + (ph ? Utf8ToWide(*ph) : L"(?)");

        if (prog) {
            wchar_t buf[64];
            swprintf_s(buf, L"  %.1f%%", (*prog) * 100.0);
            ui += buf;
        }
        ui += L"\n";
        ui += L"msg=" + (msg ? Utf8ToWide(*msg) : L"");
    } else {
        ui = L"[poll] 待機中 (status.json を検索中...)";
    }

    // ---- result.json ----
    if (auto r = ReadAllText(resultp)) {
        auto success = ExtractJsonBool(*r, "success");
        auto out_mode = ExtractJsonString2(*r, "output_mode");
        auto outp = ExtractJsonString2(*r, "output_video_path");
        auto insert_mode_s = ExtractJsonString2(*r, "insert_mode");
        auto maskp = ExtractJsonString2(*r, "mask_video_path");
        auto err = ExtractJsonString2(*r, "error_message");
        auto nf = ExtractJsonNumber(*r, "num_frames");
        auto ofps = ExtractJsonNumber(*r, "fps");

        if (success && *success) {
            ui += L"\nresult: success=true";
            if (out_mode) ui += L"  mode=" + Utf8ToWide(*out_mode);

            UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);

            if (!g_job_done && outp && !outp->empty()) {
                std::wstring outw = Utf8ToWide(*outp);
                // ファイル存在＆サイズチェック（書き込み途中対策）
                if (!FileExistsW(outw) || FileSizeSafe(fs::path(outw)) == 0) {
                    ui += L"\n(挿入) 出力ファイルの生成を待機中...";
                    UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);
                    return;
                } else {
                    // ---- 以降: 要件に合わせて「挿入モード」で挙動を分岐 ----
                    // 透過: fg(黒背景) + mask を前提に aliasへ「マスク適用（α化）」を追記して挿入
                    // GB/BB: 合成動画のみ挿入（マスク効果なし）

                    // focus_alias を jobdir から読む（job作成時に保存済み）
                    auto alias = ReadAllText(g_last_job_dir / L"focus_alias_utf8.txt");
                    if (!alias || alias->empty()) {
                        ui += L"\n(挿入) FAILED: focus_alias_utf8.txt が見つからないか空です。";
                        MessageBoxW(g_hwnd, L"focus_alias_utf8.txt が見つかりません。もう一度「取得」→「実行」を行ってください。", L"SAM3", MB_OK);
                        UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);
                        return;
                    }


                    InsertMode im = InsertMode::GB;
                    if (insert_mode_s) im = InsertModeFromUtf8(*insert_mode_s);

					// Build inserted alias according to requirements:
					//  - Transparent: duplicate ORIGINAL video + upsert alpha mask effect
					//  - GB/BB: replace video path with composited output (drop playback lines as current function does)
					std::string patched;
					if (im == InsertMode::Transparent) {
						patched = *alias; // keep original video so "filter OFF => original"
						if (maskp && !maskp->empty()) {
                            // マスクは「取得時の再生範囲」だけ生成しているので、その基準(開始/終了秒)を alias に埋め込む
                            // これにより、分割後に「動画ファイル」の再生位置が進んでもマスクが先頭に戻らない
                            patched = PatchAliasUpsertAlphaMaskEffect(
                                patched,
                                *maskp,
                                g_last_focus.playback_start_sec,
                                g_last_focus.playback_end_sec
                            );
						} else {
							ui += L"\n[warn] insert_mode=transparent but mask_video_path is empty.";
						}
					} else {
                        bool replaced_video_path = false;
						patched = PatchAliasReplaceVideoFilePath(*alias, *outp, &replaced_video_path);
                        if (!replaced_video_path) {
                            ui += L"\n(insert) FAILED: could not replace video file path in alias.";
                            MessageBoxW(
                                g_hwnd,
                                L"Could not replace video file path in alias.\nCheck focus_alias_utf8.txt.",
                                L"SAM3",
                                MB_OK
                            );
                            UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);
                            StopPollingTimer(g_hwnd);
                            CleanupChildProcessAfterJobDone(g_last_job_dir);
                            return;
                        }
					}

                    ApplyCtx actx;
                    // create_object_from_alias は alias 内 frame 情報で length を上書きしうるため、
                    // ここで必ず frame=0,<len> に正規化して “意図した長さ” を固定する
                    int expected_len = (g_last_focus.end_frame >= g_last_focus.start_frame)
                        ? (g_last_focus.end_frame - g_last_focus.start_frame + 1) : 1;
                    patched = NormalizeAliasObjectFrameHeader(patched, expected_len);
                    actx.alias_utf8 = std::move(patched);
                    actx.frame = (g_last_focus.start_frame >= 0) ? g_last_focus.start_frame : 0;
                    actx.base_layer = (g_last_focus.layer >= 0) ? g_last_focus.layer : 0;
                    actx.fallback_length = (g_last_focus.end_frame >= g_last_focus.start_frame)
                        ? (g_last_focus.end_frame - g_last_focus.start_frame + 1) : 1;
                    actx.out_num_frames = (nf && *nf > 0) ? (int)std::llround(*nf) : 0;
                    actx.out_fps = (ofps && *ofps > 0.0) ? *ofps : 0.0;
                    actx.name_w = (im == InsertMode::Transparent) ? L"SAM3 src+mask (alpha)" : L"SAM3 output (GB/BB)";
                    actx.focus_after = true;

                    // auto-hide source object (opacity=100 + audio disable)
					actx.hide_source = true;
					actx.src_layer = actx.base_layer;
					actx.src_frame = actx.frame;
					actx.src_alias_utf8 = *alias; // original alias (for source identification)
                    actx.log_dir = g_last_job_dir;

                    g_apply_ctx = &actx;
                    bool ok2 = g_edit->call_edit_section(ApplyCreateProc);
                    g_apply_ctx = nullptr;

                    if (ok2 && actx.created) {
                        ui += L"\n(挿入) 完了: レイヤー=" + std::to_wstring(actx.used_layer)
                            + L" length=" + std::to_wstring(actx.used_length);
                        g_job_done = true;
                    } else {
                        ui += L"\n(挿入) 失敗: オブジェクトの生成に失敗しました";
                        MessageBoxW(g_hwnd, L"オブジェクトの挿入に失敗しました。\nlauncher.log.txt または patched_alias_utf8.txt を確認してください。", L"SAM3", MB_OK);
                    }

                    // デバッグ用に patched を保存
                    WriteTextFileUtf8Atomic(g_last_job_dir / L"patched_alias_after_insert_utf8.txt", actx.alias_utf8);
                    UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);
                }
            }

            // Job done: stop polling and cleanup python handles/process (python should already have exited).
            StopPollingTimer(g_hwnd);
            CleanupChildProcessAfterJobDone(g_last_job_dir);
            return;
        }

        if (success && !*success) {
            ui += L"\nresult: success=false";
            if (err && !err->empty()) ui += L"\nerror=" + Utf8ToWide(*err);

            UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);

            // Job done (failed): stop polling and cleanup python handles/process.
            StopPollingTimer(g_hwnd);
            CleanupChildProcessAfterJobDone(g_last_job_dir);
            return;
        }
    }

    UpdateUiText(g_hwnd, g_last_focus, g_last_job_dir, ui);
}

static InsertMode GetInsertModeFromUi() {
    if (!g_combo_insert) return InsertMode::Transparent;
    int idx = (int)SendMessageW(g_combo_insert, CB_GETCURSEL, 0, 0);
    wchar_t buf[32]{};
    if (idx >= 0) {
        SendMessageW(g_combo_insert, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)buf);
        std::wstring w = buf;
        if (w == L"BB") return InsertMode::BB;
        if (w == L"GB") return InsertMode::GB;
    }
    return InsertMode::Transparent; // default
}


// ---- UI ----
static void CreateChildControls(HWND hwnd) {
    g_btn_capture = CreateWindowW(L"BUTTON", L"1) 選択オブジェクト取得", WS_CHILD | WS_VISIBLE,
        10, 10, 220, 28, hwnd, (HMENU)1001, g_hmod, nullptr);

    g_edit_path = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        10, 44, 520, 24, hwnd, (HMENU)1002, g_hmod, nullptr);

    CreateWindowW(L"STATIC", L"挿入:", WS_CHILD | WS_VISIBLE,
        240, 12, 50, 22, hwnd, (HMENU)1101, g_hmod, nullptr);

    g_combo_insert = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        295, 10, 120, 300, hwnd, (HMENU)1102, g_hmod, nullptr);
    
    if (g_combo_insert) {
        SendMessageW(g_combo_insert, CB_RESETCONTENT, 0, 0);
        SendMessageW(g_combo_insert, CB_ADDSTRING, 0, (LPARAM)L"透過"); // default
        SendMessageW(g_combo_insert, CB_ADDSTRING, 0, (LPARAM)L"GB");
        SendMessageW(g_combo_insert, CB_ADDSTRING, 0, (LPARAM)L"BB");
        SendMessageW(g_combo_insert, CB_SETCURSEL, 0, 0); // default 透過
    }

    g_btn_run = CreateWindowW(L"BUTTON", L"2) 実行", WS_CHILD | WS_VISIBLE,
        10, 76, 220, 28, hwnd, (HMENU)1003, g_hmod, nullptr);

    g_edit_job = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        10, 110, 520, 24, hwnd, (HMENU)1004, g_hmod, nullptr);

    g_btn_open = CreateWindowW(L"BUTTON", L"切り抜き画面を開く", WS_CHILD | WS_VISIBLE,
        240, 76, 140, 28, hwnd, (HMENU)1005, g_hmod, nullptr);

    g_static_status = CreateWindowW(L"STATIC", L"Status: 待機中", WS_CHILD | WS_VISIBLE,
        10, 142, 520, 42, hwnd, (HMENU)1006, g_hmod, nullptr);
}


static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateChildControls(hwnd);
        ApplyUiFont(hwnd);
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc{};
        GetClientRect(hwnd, &rc);

        // WNDCLASS の hbrBackground と同じ系統で塗る
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_MENU));

        return 1; // 背景消去は自前で完了
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 1001) {
            if (!g_edit) {
                UpdateUiText(hwnd, g_last_focus, g_last_job_dir, L"Status: 編集ハンドルが無効です");
                return 0;
            }
            // EDIT_HANDLE で安全に edit section を呼ぶ :contentReference[oaicite:8]{index=8}
            FocusVideoInfo cap{};
            g_capture_dst = &cap;
            bool ok = g_edit->call_edit_section(CaptureProc);
            g_capture_dst = nullptr;

            if (!ok) {
                UpdateUiText(hwnd, g_last_focus, g_last_job_dir, L"Status: 編集情報の取得失敗");
                return 0;
            }
            g_last_focus = cap;

            UpdateUiText(hwnd, g_last_focus, g_last_job_dir,
                cap.ok ? (L"Status: オブジェクト情報を取得しました") : (L"Status: 情報の取得に失敗しました"));
            return 0;
        }

        if (id == 1003) {
            if (!g_last_focus.ok) {
                UpdateUiText(hwnd, g_last_focus, g_last_job_dir, L"Status: 先に「選択オブジェクト取得」を行ってください");
                return 0;
            }
            g_job_done = false;
            JobsGCKeepLatestN(20);

            std::string job_id = MakeJobId();
            fs::path jobdir = JobsRootDir() / Utf8ToWide(job_id);
            std::error_code ec;
            fs::create_directories(jobdir, ec);
            if (ec) {
                UpdateUiText(hwnd, g_last_focus, g_last_job_dir, L"Status: ジョブフォルダの作成に失敗しました");
                return 0;
            }

            // output_dir: とりあえず Plugins/SAM3/output
            fs::path outdir = PluginRootDir() / L"output";
            fs::create_directories(outdir, ec);
            ec.clear();

            InsertMode im = GetInsertModeFromUi();
            std::string insert_mode = InsertModeToUtf8(im);
            std::string bg = BgModeFromInsertMode(im);
            std::string req = BuildRequestJsonV1(job_id, g_last_focus, outdir, bg, insert_mode);
            if (!WriteTextFileUtf8Atomic(jobdir / L"request.json", req)) {
                UpdateUiText(hwnd, g_last_focus, jobdir, L"Status: request.json の書き込みに失敗しました");
                return 0;
            }

            // alias dump（item 名確定のため）
            if (!g_last_focus.alias_utf8.empty()) {
                WriteTextFileUtf8Atomic(jobdir / L"focus_alias_utf8.txt", g_last_focus.alias_utf8);
            }

            // 初期 status
            std::string st =
                "{\n"
                "  \"state\": \"queued\",\n"
                "  \"prompt_mode\": \"click\",\n"
                "  \"output_mode\": \"fgmask\",\n"
                "  \"phase\": \"boot\",\n"
                "  \"progress\": 0.0,\n"
                "  \"message\": \"Launching python...\",\n"
                "  \"gradio_url\": \"\",\n"
                "  \"updated_at_jst\": \"\"\n"
                "}\n";
            WriteTextFileUtf8Atomic(jobdir / L"status.json", st);

            // Python 起動
            if (!LaunchPythonJob(jobdir)) {
                UpdateUiText(hwnd, g_last_focus, jobdir, L"Status: Pythonの起動に失敗 (パスを確認してください)");
                return 0;
            }

            g_last_job_dir = jobdir;

            // poll timer
            if (g_timer_id) KillTimer(hwnd, g_timer_id);
            g_timer_id = SetTimer(hwnd, 2001, 300, nullptr);

            UpdateUiText(hwnd, g_last_focus, g_last_job_dir, L"Status: 処理中... (Python起動成功)");
            return 0;
        }

        if (id == 1005) {
            fs::path jobdir = GetJobDirFromUiOrLast();
            auto urlw = ReadGradioUrlWithRetry(jobdir, 40, 50);

            if (urlw && !urlw->empty()) {
                ShellExecuteW(nullptr, L"open", urlw->c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                std::wstring msg = L"WebUIのURLがまだ取得できていません。\n\n参照先:\n" + (jobdir / L"status.json").wstring();
                MessageBoxW(hwnd, msg.c_str(), L"SAM3", MB_OK);
            }
            return 0;
        }

        return 0;
    }

    case WM_TIMER:
        if (wp == 2001) {
            PollJobFiles();
            return 0;
        }
        return 0;

    case WM_DESTROY:
        // stop polling first
        StopPollingTimer(hwnd);

        // If python is still running (panel closed mid-job), force-kill to avoid orphan processes.
        if (g_child_proc || g_child_job) {
            fs::path jd = g_last_job_dir;
            ForceKillChildProcessTree(jd, L"panel destroy");
        }
        if (g_ui_font) {
            DeleteObject(g_ui_font);
            g_ui_font = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND CreatePanelWindow() {
    const wchar_t* kClassName = L"SAM3_AUX2_PANEL";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelWndProc;
    wc.hInstance = g_hmod;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    wc.hbrBackground = GetSysColorBrush(COLOR_MENU);

    if (!RegisterClassExW(&wc)) {
        DWORD e = GetLastError();
        if (e != ERROR_CLASS_ALREADY_EXISTS) {
            return nullptr;
        }
    }

    // WS_OVERLAPPEDWINDOW は使わない（タイトルバー等が残る）
    // register_window_client() が WS_CHILD + 親付与を行う前提
    DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    DWORD exstyle = 0;

    HWND hwnd = CreateWindowExW(
        exstyle,
        kClassName,
        L"SAM3",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 220,
        nullptr, nullptr,
        g_hmod,
        nullptr
    );
    return hwnd;
}

extern "C" __declspec(dllexport)
void RegisterPlugin(HOST_APP_TABLE* host)
{
    g_host = host;

    host->set_plugin_information(L"SAM3");

    g_edit = host->create_edit_handle();

    g_hwnd = CreatePanelWindow();
    if (g_hwnd) {
        host->register_window_client(L"SAM3", g_hwnd);
    }
}
