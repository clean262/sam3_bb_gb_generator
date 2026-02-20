//----------------------------------------------------------------------------------
//	フィルタプラグイン ヘッダーファイル for AviUtl ExEdit2
//	By ＫＥＮくん
//----------------------------------------------------------------------------------

//	フィルタプラグインは下記の関数を外部公開すると呼び出されます
//
//	フィルタ構造体のポインタを渡す関数 (必須)
//		FILTER_PLUGIN_TABLE* GetFilterPluginTable(void)
//
//	必要とする本体バージョン番号取得関数 (任意)
//		DWORD RequiredVersion() ※必要な本体のバージョン番号を返却します
//
//	プラグインDLL初期化関数 (任意)
//		bool InitializePlugin(DWORD version) ※versionは本体のバージョン番号
// 
//	プラグインDLL終了関数 (任意)
//		void UninitializePlugin()
// 
//	ログ出力機能初期化関数 (任意) ※logger2.h
//		void InitializeLogger(LOG_HANDLE* logger)
// 
//	設定関連機能初期化関数 (任意) ※config2.h
//		void InitializeConfig(CONFIG_HANDLE* config)

//----------------------------------------------------------------------------------

struct EDIT_SECTION;

// トラックバー項目構造体
// 例：FILTER_ITEM_TRACK track = { L"数値", 100.0, 0.0, 1000.0, 0.01 };
struct FILTER_ITEM_TRACK {
	FILTER_ITEM_TRACK(LPCWSTR name, double value, double s, double e, double step = 1.0) : name(name), value(value), s(s), e(e), step(step) {}
	LPCWSTR type = L"track";	// 設定の種別
	LPCWSTR name;				// 設定名
	double value;				// 設定値 (フィルタ処理の呼び出し時に現在の値に更新されます)
	const double s, e;			// 設定値の最小、最大
	const double step;			// 設定値の単位( 1.0 / 0.1 / 0.01 / 0.001 )
};

// チェックボックス項目構造体
// 例：FILTER_ITEM_CHECK check = { L"チェック", false };
struct FILTER_ITEM_CHECK {
	FILTER_ITEM_CHECK(LPCWSTR name, bool value) : name(name), value(value) {}
	LPCWSTR type = L"check";	// 設定の種別
	LPCWSTR name;				// 設定名
	bool value;					// 設定値 (フィルタ処理の呼び出し時に現在の値に更新されます)
};

// 色選択項目構造体
// 例：FILTER_ITEM_COLOR color = { L"色", 0xffffff };
struct FILTER_ITEM_COLOR {
	union VALUE { // 設定値の色
		int code;
		struct { unsigned char b, g, r, x; };
	};
	FILTER_ITEM_COLOR(LPCWSTR name, int code) : name(name) { value.code = code; }
	LPCWSTR type = L"color";	// 設定の種別
	LPCWSTR name;				// 設定名
	VALUE value;				// 設定値 (フィルタ処理の呼び出し時に現在の値に更新されます)
};

// 選択リスト項目構造体
// 例：FILTER_ITEM_SELECT::ITEM list[] = { { L"項目1", 1 }, { L"項目2", 2 }, { L"項目3", 3 }, { nullptr } };
//     FILTER_ITEM_SELECT selectItem = { L"選択", 1, list };
struct FILTER_ITEM_SELECT {
	struct ITEM {		// 選択肢項目
		LPCWSTR name;	// 選択肢の名前
		int value;		// 選択肢の値
	};
	FILTER_ITEM_SELECT(LPCWSTR name, int value, ITEM* list) : name(name), value(value), list(list) {}
	LPCWSTR type = L"select";	// 設定の種別
	LPCWSTR name;				// 設定名
	int value;					// 設定値 (フィルタ処理の呼び出し時に現在の値に更新されます)
	ITEM* list;					// 選択肢リスト (ITEMを列挙して名前がnullのITEMで終端したリストへのポインタ)
};

// ファイル選択項目構造体
// 例：FILTER_ITEM_FILE file = { L"ファイル", L"", L"AviFile (*.avi)\0*.avi\0" };
struct FILTER_ITEM_FILE {
	FILTER_ITEM_FILE(LPCWSTR name, LPCWSTR value, LPCWSTR filefilter) : name(name), value(value), filefilter(filefilter) {}
	LPCWSTR type = L"file";		// 設定の種別
	LPCWSTR name;				// 設定名
	LPCWSTR value;				// 設定値 (フィルタ処理の呼び出し時に現在の値のポインタに更新されます)
	LPCWSTR filefilter;			// ファイルフィルタ
};

// 汎用データ項目構造体 (設定が表示されない項目になります)
// フィルタ処理関数内でvalueの参照先データを更新することが出来ます
// 例：struct Data {
//       int   item1 = 1;
//       float item2 = 2.0f;
//     };
//     FILTER_ITEM_DATA<Data> data = { L"データ" };
template<typename T>
struct FILTER_ITEM_DATA {
	FILTER_ITEM_DATA(LPCWSTR name) : name(name), size(sizeof(T)), value(&default_value) {}
	LPCWSTR type = L"data";		// 設定の種別
	LPCWSTR name;				// 設定名
	T* value;					// 設定値 (フィルタ処理の呼び出し時に現在の値のポインタに更新されます)
	const int size;				// 汎用データのサイズ(1024バイト以下)
	T default_value;			// デフォルト値 (Tの定義でデフォルト値を指定しておく)
};

// 設定グループ項目構造体
// 自身以降の設定項目をグループ化することが出来ます
// ※設定名を空にするとグループの終端を定義することが出来ます
// 例：FILTER_ITEM_GROUP group = { L"座標" };
struct FILTER_ITEM_GROUP {
	FILTER_ITEM_GROUP(LPCWSTR name, bool default_visible = true) : name(name), default_visible(default_visible) {}
	LPCWSTR type = L"group";	// 設定の種別
	LPCWSTR name;				// 設定名
	const bool default_visible;	// デフォルトの表示状態
};

// ボタン項目構造体
// ボタンを押すとコールバック関数が呼ばれます ※plugin2.hの編集のコールバック関数と同様な形になります
// 例：FILTER_ITEM_BUTTON button = { L"初期化", [](EDIT_SECTION* edit) { /* ボタンを押した時の処理 */ } };
struct FILTER_ITEM_BUTTON {
	FILTER_ITEM_BUTTON(LPCWSTR name, void (*callback)(EDIT_SECTION* edit)) : name(name), callback(callback) {}
	LPCWSTR type = L"button";			// 設定の種別
	LPCWSTR name;						// 設定名
	void (*callback)(EDIT_SECTION*);	// ボタンを押した時のコールバック関数
};

// 文字列項目構造体 ※1行の文字列
// 例：FILTER_ITEM_STRING string = { L"文字列", L"" };
struct FILTER_ITEM_STRING {
	FILTER_ITEM_STRING(LPCWSTR name, LPCWSTR value) : name(name), value(value) {}
	LPCWSTR type = L"string";	// 設定の種別
	LPCWSTR name;				// 設定名
	LPCWSTR value;				// 設定値 (フィルタ処理の呼び出し時に現在の値のポインタに更新されます)
};

// テキスト項目構造体 ※複数行の文字列
// 例：FILTER_ITEM_TEXT text = { L"テキスト", L"" };
struct FILTER_ITEM_TEXT {
	FILTER_ITEM_TEXT(LPCWSTR name, LPCWSTR value) : name(name), value(value) {}
	LPCWSTR type = L"text";		// 設定の種別
	LPCWSTR name;				// 設定名
	LPCWSTR value;				// 設定値 (フィルタ処理の呼び出し時に現在の値のポインタに更新されます)
};

// フォルダ選択項目構造体
// 例：FILTER_ITEM_FOLDER folder = { L"フォルダ", L"" };
struct FILTER_ITEM_FOLDER {
	FILTER_ITEM_FOLDER(LPCWSTR name, LPCWSTR value) : name(name), value(value) {}
	LPCWSTR type = L"folder";	// 設定の種別
	LPCWSTR name;				// 設定名
	LPCWSTR value;				// 設定値 (フィルタ処理の呼び出し時に現在の値のポインタに更新されます)
};

//----------------------------------------------------------------------------------

// RGBA32bit構造体
struct PIXEL_RGBA {
	unsigned char r, g, b, a;
};

// シーン情報構造体
struct SCENE_INFO {
	int width, height;		// シーンの解像度
	int rate, scale;		// シーンのフレームレート
	int sample_rate;		// シーンのサンプリングレート
};

// オブジェクト情報構造体
struct OBJECT_INFO {
	int64_t id;				// オブジェクトのID (アプリ起動毎の固有ID)
							// ※描画対象のオブジェクトの固有ID
	int frame;				// オブジェクトの現在のフレーム番号
	int frame_total;		// オブジェクトの総フレーム数
	double time;			// オブジェクトの現在の時間(秒)
	double time_total;		// オブジェクトの総時間(秒)
	int width, height;		// オブジェクトの現在の画像サイズ (画像フィルタのみ)
	int64_t sample_index;	// オブジェクトの現在の音声サンプル位置 (音声フィルタのみ)
	int64_t sample_total;	// オブジェクトの総サンプル数 (音声フィルタのみ)
	int sample_num;			// オブジェクトの現在の音声サンプル数 (音声フィルタのみ)
	int channel_num;		// オブジェクトの現在の音声チャンネル数 (音声フィルタのみ) ※通常2になります
	int64_t effect_id;		// オブジェクトの内の対象エフェクトのID (アプリ起動毎の固有ID)
							// ※処理対象のフィルタ効果、オブジェクト入出力の固有ID
	int flag;				// フラグ
	static constexpr int FLAG_FILTER_OBJECT = 1;	// フィルタオブジェクトか？
	inline bool is_filter_object() const { return flag & FLAG_FILTER_OBJECT; }
};

//----------------------------------------------------------------------------------

// d3d11.h向けの前方宣言 ※includeしないで良いように定義
struct ID3D11Texture2D;

// 画像フィルタ処理用構造体
struct FILTER_PROC_VIDEO {
	// シーン情報
	const SCENE_INFO* scene;

	// オブジェクト情報
	const OBJECT_INFO* object;

	// 現在のオブジェクトの画像データを取得する (VRAMからデータを取得します) 
	// buffer		: 画像データの格納先へのポインタ
	void (*get_image_data)(PIXEL_RGBA* buffer);

	// 現在のオブジェクトの画像データを設定します (VRAMへデータを書き込みます) 
	// buffer		: 画像データへのポインタ (nullptrの場合は初期データ無しで画像サイズを変更します)
	// width,height	: 画像サイズ
	void (*set_image_data)(PIXEL_RGBA* buffer, int width, int height);

	// 現在のオブジェクトの画像データのポインタを取得する (ID3D11Texture2Dのポインタを取得します) 
	// 戻り値		: オブジェクトの画像データへのポインタ
	//				  ※現在の画像が変更(set_image_data)されるかフィルタ処理の終了まで有効
	ID3D11Texture2D* (*get_image_texture2d)();

	// 現在のフレームバッファの画像データのポインタを取得する (ID3D11Texture2Dのポインタを取得します) 
	// 戻り値		: フレームバッファの画像データへのポインタ
	//				  ※フィルタ処理の終了まで有効
	ID3D11Texture2D* (*get_framebuffer_texture2d)();

};

// 音声フィルタ処理用構造体
struct FILTER_PROC_AUDIO {
	// シーン情報
	const SCENE_INFO* scene;

	// オブジェクト情報
	const OBJECT_INFO* object;

	// 現在のオブジェクトの音声データを取得する
	// buffer		: 音声データの格納先へのポインタ ※音声データはPCM(float)32bit
	// channel		: 音声データのチャンネル ( 0 = 左チャンネル / 1 = 右チャンネル )
	void (*get_sample_data)(float* buffer, int channel);

	// 現在のオブジェクトの音声データを設定する
	// buffer		: 音声データへのポインタ ※音声データはPCM(float)32bit
	// channel		: 音声データのチャンネル ( 0 = 左チャンネル / 1 = 右チャンネル )
	void (*set_sample_data)(float* buffer, int channel);

};

//----------------------------------------------------------------------------------

// フィルタプラグイン構造体
struct FILTER_PLUGIN_TABLE {
	int flag;								// フラグ
	static constexpr int FLAG_VIDEO = 1;	// 画像フィルタをサポートする
	static constexpr int FLAG_AUDIO = 2;	// 音声フィルタをサポートする
											// 画像と音声のフィルタ処理は別々のスレッドで処理されます
	static constexpr int FLAG_INPUT = 4;	// メディアオブジェクトの初期入力をする (メディアオブジェクトにする場合)
	static constexpr int FLAG_FILTER = 8;	// フィルタオブジェクトをサポートする (フィルタオブジェクトに対応する場合)
											// フィルタオブジェクトの場合は画像サイズの変更が出来ません
	LPCWSTR name;				// プラグインの名前
	LPCWSTR label;				// ラベルの初期値 (nullptrならデフォルトのラベルになります)
	LPCWSTR information;		// プラグインの情報
	void** items;				// 設定項目の定義 (FILTER_ITEM_XXXポインタを列挙してnull終端したリストへのポインタ)

	// 画像フィルタ処理関数へのポインタ (FLAG_VIDEOが有効の時のみ呼ばれます)
	bool (*func_proc_video)(FILTER_PROC_VIDEO* video);

	// 音声フィルタ処理関数へのポインタ (FLAG_AUDIOが有効の時のみ呼ばれます)
	bool (*func_proc_audio)(FILTER_PROC_AUDIO* audio);

};
