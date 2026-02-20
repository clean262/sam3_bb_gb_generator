//----------------------------------------------------------------------------------
//	汎用プラグイン ヘッダーファイル for AviUtl ExEdit2
//	By ＫＥＮくん
//----------------------------------------------------------------------------------

//	汎用プラグインは下記の関数を外部公開すると呼び出されます
//
//	プラグイン登録関数 (必須)
//		void RegisterPlugin(HOST_APP_TABLE* host)
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

struct INPUT_PLUGIN_TABLE;
struct OUTPUT_PLUGIN_TABLE;
struct FILTER_PLUGIN_TABLE;
struct SCRIPT_MODULE_TABLE;
struct EDIT_HANDLE;
struct PROJECT_FILE;

// オブジェクトハンドル
typedef void* OBJECT_HANDLE;

// レイヤー・フレーム情報構造体
// フレーム番号、レイヤー番号が0からの番号になります ※UI表示と異なります
struct OBJECT_LAYER_FRAME {
	int layer;	// レイヤー番号
	int start;	// 開始フレーム番号
	int end;	// 終了フレーム番号
};

// メディア情報構造体
struct MEDIA_INFO {
	int video_track_num;	// Videoトラック数 ※0ならVideo無し
	int audio_track_num;	// Audioトラック数 ※0ならAudio無し
	double total_time;		// 総時間 ※静止画の場合は0
	int width, height;		// 解像度
};

// モジュール情報構造体
struct MODULE_INFO {
	int type;
	static constexpr int TYPE_SCRIPT_FILTER	= 1;	// フィルタスクリプト
	static constexpr int TYPE_SCRIPT_OBJECT	= 2;	// オブジェクトスクリプト
	static constexpr int TYPE_SCRIPT_CAMERA	= 3;	// カメラスクリプト
	static constexpr int TYPE_SCRIPT_TRACK	= 4;	// トラックバースクリプト
	static constexpr int TYPE_SCRIPT_MODULE	= 5;	// スクリプトモジュール
	static constexpr int TYPE_PLUGIN_INPUT	= 6;	// 入力プラグイン
	static constexpr int TYPE_PLUGIN_OUTPUT	= 7;	// 出力プラグイン
	static constexpr int TYPE_PLUGIN_FILTER	= 8;	// フィルタプラグイン
	static constexpr int TYPE_PLUGIN_COMMON	= 9;	// 汎用プラグイン
	LPCWSTR name;
	LPCWSTR information;
};

//----------------------------------------------------------------------------------

// 編集情報構造体
// フレーム番号、レイヤー番号が0からの番号になります ※UI表示と異なります
struct EDIT_INFO {
	int width, height;	// シーンの解像度
	int rate, scale;	// シーンのフレームレート
	int sample_rate;	// シーンのサンプリングレート
	int frame;			// 現在のカーソルのフレーム番号
	int layer;			// 現在の選択レイヤー番号
	int frame_max;		// オブジェクトが存在する最大のフレーム番号
	int layer_max;		// オブジェクトが存在する最大のレイヤー番号
	int display_frame_start;	// レイヤー編集で表示されているフレームの開始番号
	int display_layer_start;	// レイヤー編集で表示されているレイヤーの開始番号
	int display_frame_num;		// レイヤー編集で表示されているフレーム数 ※厳密ではないです
	int display_layer_num;		// レイヤー編集で表示されているレイヤー数 ※厳密ではないです
	int select_range_start;		// フレーム範囲選択の開始フレーム番号 ※未選択の場合は-1
	int select_range_end;		// フレーム範囲選択の終了フレーム番号 ※未選択の場合は-1
	float grid_bpm_tempo;		// グリッド(BPM)のテンポ
	int grid_bpm_beat;			// グリッド(BPM)の拍子
	float grid_bpm_offset;		// グリッド(BPM)の基準時間
	int scene_id;		// シーンのID
};

// 編集セクション構造体
// メニュー選択やプロジェクト編集のコールバック関数内で利用出来ます
// フレーム番号、レイヤー番号が0からの番号になります ※UI表示と異なります
struct EDIT_SECTION {
	// 編集情報
	EDIT_INFO* info;

	// 指定の位置にオブジェクトエイリアスを作成します
	// alias	: オブジェクトエイリアスデータ(UTF-8)へのポインタ
	//			  オブジェクトエイリアスファイル(.object)と同じフォーマットになります
	// layer	: 作成するレイヤー番号
	// frame	: 作成するフレーム番号
	// length	: オブジェクトのフレーム数 ※エイリアスデータにフレーム情報がある場合はフレーム情報から長さが設定されます
	//			  フレーム数に0を指定した場合は長さや追加位置が自動調整されます
	// 戻り値	: 作成したオブジェクトのハンドル (失敗した場合はnullptrを返却)
	//			  既に存在するオブジェクトに重なったり、エイリアスデータが不正な場合に失敗します
	//			  複数オブジェクトのエイリアスデータの場合は先頭のオブジェクトのハンドルが返却されます ※オブジェクトは全て作成されます
	OBJECT_HANDLE (*create_object_from_alias)(LPCSTR alias, int layer, int frame, int length);

	// 指定のフレーム番号以降にあるオブジェクトを検索します
	// layer	: 検索対象のレイヤー番号
	// frame	: 検索を開始するフレーム番号
	// 戻り値	: 検索したオブジェクトのハンドル (見つからない場合はnullptrを返却)
	OBJECT_HANDLE (*find_object)(int layer, int frame);

	// オブジェクトに対象エフェクトが何個存在するかを取得します
	// object	: オブジェクトのハンドル
	// effect	: 対象のエフェクト名 (エイリアスファイルのeffect.nameの値)
	// 戻り値	: 対象エフェクトの数 ※存在しない場合は0
	int (*count_object_effect)(OBJECT_HANDLE object, LPCWSTR effect);

	// オブジェクトのレイヤー・フレーム情報を取得します
	// object	: オブジェクトのハンドル
	// 戻り値	: オブジェクトのレイヤー・フレーム情報
	OBJECT_LAYER_FRAME (*get_object_layer_frame)(OBJECT_HANDLE object);

	// オブジェクトのエイリアスデータを取得します
	// object	: オブジェクトのハンドル
	// 戻り値	: オブジェクトエイリアスデータ(UTF-8)へのポインタ (取得出来ない場合はnullptrを返却)
	// 			  オブジェクトエイリアスファイルと同じフォーマットになります
	//			  ※次に文字列返却の関数を使うかコールバック処理の終了まで有効
	LPCSTR (*get_object_alias)(OBJECT_HANDLE object);

	// オブジェクトの設定項目の値を文字列で取得します
	// object	: オブジェクトのハンドル
	// effect	: 対象のエフェクト名 (エイリアスファイルのeffect.nameの値)
	//			  同じエフェクトが複数ある場合は":n"のサフィックスでインデックス指定出来ます (nは0からの番号)
	//			  get_object_item_value(object, L"ぼかし:1", L"範囲"); // 2個目のぼかしを対象とする
	// item		: 対象の設定項目の名称 (エイリアスファイルのキーの名称)
	// 戻り値	: 取得した設定値(UTF8)へのポインタ (取得出来ない場合はnullptrを返却)
	//			  エイリアスファイルの設定値と同じフォーマットになります
	//			  ※次に文字列返却の関数を使うかコールバック処理の終了まで有効
	LPCSTR (*get_object_item_value)(OBJECT_HANDLE object, LPCWSTR effect, LPCWSTR item);

	// オブジェクトの設定項目の値を文字列で設定します
	// object	: オブジェクトのハンドル
	// effect	: 対象のエフェクト名 (エイリアスファイルのeffect.nameの値)
	//			  同じエフェクトが複数ある場合は":n"のサフィックスでインデックス指定出来ます (nは0からの番号)
	//			  set_object_item_value(object, L"ぼかし:1", L"範囲", "1"); // 2個目のぼかしを対象とする
	// item		: 対象の設定項目の名称 (エイリアスファイルのキーの名称)
	// value	: 設定値(UTF8)
	//			  エイリアスファイルの設定値と同じフォーマットになります
	// 戻り値	: 設定出来た場合はtrue (対象が見つからない場合は失敗します)
	bool (*set_object_item_value)(OBJECT_HANDLE object, LPCWSTR effect, LPCWSTR item, LPCSTR value);

	// オブジェクトを移動します
	// object	: オブジェクトのハンドル
	// layer	: 移動先のレイヤー番号
	// frame	: 移動先のフレーム番号
	// 戻り値	: 移動した場合はtrue (移動先にオブジェクトが存在する場合は失敗します)
	bool (*move_object)(OBJECT_HANDLE object, int layer, int frame);

	// オブジェクトを削除します
	// object	: オブジェクトのハンドル
	void (*delete_object)(OBJECT_HANDLE object);

	// オブジェクト設定ウィンドウで選択されているオブジェクトのハンドルを取得します
	// 戻り値	: オブジェクトのハンドル (未選択の場合はnullptrを返却)　
	OBJECT_HANDLE (*get_focus_object)();

	// オブジェクト設定ウィンドウで選択するオブジェクトを設定します (コールバック処理の終了時に設定されます)
	// object	: オブジェクトのハンドル
	void (*set_focus_object)(OBJECT_HANDLE object);

	// プロジェクトファイルのポインタを取得します
	// EDIT_HANDLE	: 編集ハンドル
	// 戻り値		: プロジェクトファイル構造体へのポインタ
	//				  ※コールバック処理の終了まで有効
	PROJECT_FILE* (*get_project_file)(EDIT_HANDLE* edit);

	// 選択中オブジェクトのハンドルを取得します
	// index	: 選択中オブジェクトのインデックス(0〜)
	// 戻り値	: 指定インデックスのオブジェクトのハンドル (インデックスが範囲外の場合はnullptrを返却)
	OBJECT_HANDLE (*get_selected_object)(int index);

	// 選択中オブジェクトの数を取得します
	// 戻り値	: 選択中オブジェクトの数
	int (*get_selected_object_num)();

	// マウス座標のレイヤー・フレーム位置を取得します
	// 最後のマウス移動のウィンドウメッセージの座標から計算します
	// layer	: レイヤー番号の格納先
	// frame	: フレーム番号の格納先
	// 戻り値	: マウス座標がレイヤー編集上の場合はtrue
	bool (*get_mouse_layer_frame)(int* layer, int* frame);

	// 指定のスクリーン座標のレイヤー・フレーム位置を取得します
	// x,y		: 対象のスクリーン座標
	// layer	: レイヤー番号の格納先
	// frame	: フレーム番号の格納先
	// 戻り値	: スクリーン座標がレイヤー編集上の場合はtrue
	bool (*pos_to_layer_frame)(int x, int y, int* layer, int* frame);

	// 指定のメディアファイルがサポートされているかを確認します
	// file		: メディアファイルのパス
	// strict	: trueの場合は実際に読み込めるかを確認します
	//			  falseの場合は拡張子が対応しているかを確認します
	// 戻り値	: サポートされている場合はtrue
	bool (*is_support_media_file)(LPCWSTR file, bool strict);

	// 指定のメディアファイルの情報を取得します ※動画、音声、画像ファイル以外では取得出来ません
	// file			: メディアファイルのパス
	// info			: メディア情報の格納先へのポインタ
	// info_size	: メディア情報の格納先のサイズ ※MEDIA_INFOと異なる場合はサイズ分のみ取得されます
	// 戻り値		: 取得出来た場合はtrue
	bool (*get_media_info)(LPCWSTR file, MEDIA_INFO* info, int info_size);

	// 指定の位置にメディアファイルからオブジェクトを作成します
	// file		: メディアファイルのパス
	// layer	: 作成するレイヤー番号
	// frame	: 作成するフレーム番号
	// length	: オブジェクトのフレーム数
	//			  フレーム数に0を指定した場合は長さや追加位置が自動調整されます
	// 戻り値	: 作成したオブジェクトのハンドル (失敗した場合はnullptrを返却)
	//			  既に存在するオブジェクトに重なったり、メディアファイルに対応していない場合は失敗します
	OBJECT_HANDLE (*create_object_from_media_file)(LPCWSTR file, int layer, int frame, int length);

	// 指定の位置にオブジェクトを作成します
	// effect	: エフェクト名 (エイリアスファイルのeffect.nameの値)
	// layer	: 作成するレイヤー番号
	// frame	: 作成するフレーム番号
	// length	: オブジェクトのフレーム数
	//			  フレーム数に0を指定した場合は長さや追加位置が自動調整されます
	// 戻り値	: 作成したオブジェクトのハンドル (失敗した場合はnullptrを返却)
	//			  既に存在するオブジェクトに重なったり、指定エフェクトに対応していない場合は失敗します
	OBJECT_HANDLE (*create_object)(LPCWSTR effect, int layer, int frame, int length);

	// 現在のレイヤー・フレーム位置を設定します ※設定出来る範囲に調整されます
	// layer	: レイヤー番号
	// frame	: フレーム番号
	void (*set_cursor_layer_frame)(int layer, int frame);

	// レイヤー編集のレイヤー・フレームの表示開始位置を設定します ※設定出来る範囲に調整されます
	// layer	: 表示開始レイヤー番号
	// frame	: 表示開始フレーム番号
	void (*set_display_layer_frame)(int layer, int frame);

	// フレーム範囲選択を設定します ※設定出来る範囲に調整されます
	// start,end	: 開始終了フレーム番号
	//				  開始終了フレームの両方に-1を指定すると選択を解除します
	void (*set_select_range)(int start, int end);

	// グリッド(BPM)を設定します
	// tempo	: テンポ
	// beat		: 拍子
	// offset	: 基準時間
	void (*set_grid_bpm)(float tempo, int beat, float offset);

	// オブジェクト名を取得します
	// object	: オブジェクトのハンドル
	// 戻り値	: オブジェクト名へのポインタ (標準の名前の場合はnullptrを返却)　
	//			  ※オブジェクトの編集をするかコールバック処理の終了まで有効
	LPCWSTR (*get_object_name)(OBJECT_HANDLE object);

	// オブジェクト名を設定します
	// object	: オブジェクトのハンドル
	// name		: オブジェクト名 (nullptrか空文字を指定すると標準の名前になります)　
	void (*set_object_name)(OBJECT_HANDLE object, LPCWSTR name);

	// レイヤー名を取得します
	// layer	: レイヤー番号
	// 戻り値	: レイヤー名へのポインタ (標準の名前の場合はnullptrを返却)　
	//			  ※レイヤーの編集をするかコールバック処理の終了まで有効
	LPCWSTR (*get_layer_name)(int layer);

	// レイヤー名を設定します
	// layer	: レイヤー番号
	// name		: レイヤー名 (nullptrか空文字を指定すると標準の名前になります)　
	void (*set_layer_name)(int layer, LPCWSTR name);

	// シーン名を取得します
	// 戻り値	: シーン名へのポインタ
	//			  ※シーンの編集をするかコールバック処理の終了まで有効
	LPCWSTR (*get_scene_name)();

	// シーン名を設定します ※シーンの操作は現状Undoに非対応です
	// name		: シーン名
	//			  ※シーン名は必須になります (nullptrや空文字の場合は変更しません)
	void (*set_scene_name)(LPCWSTR name);

	// シーンの解像度を設定します ※シーンの操作は現状Undoに非対応です
	// width	: 横のサイズ
	// height	: 縦のサイズ
	void (*set_scene_size)(int width, int height);

	// シーンのフレームレートを設定します ※シーンの操作は現状Undoに非対応です
	// rate		: フレームレート
	// scale	: フレームレートのスケール
	void (*set_scene_frame_rate)(int rate, int scale);

	// シーンのサンプリングレートを設定します ※シーンの操作は現状Undoに非対応です
	// sample_rate	: サンプリングレート
	void (*set_scene_sample_rate)(int sample_rate);

};

// 編集ハンドル構造体
struct EDIT_HANDLE {
	// プロジェクトデータの編集をする為のコールバック関数(func_proc_edit)を呼び出します
	// 編集情報を排他制御する為に更新ロック状態のコールバック関数内で編集処理をする形になります
	// コールバック関数内で編集したオブジェクトは纏めてUndoに登録されます
	// コールバック関数はメインスレッドから呼ばれます
	// func_proc_edit	: 編集処理のコールバック関数
	// 戻り値			: trueなら成功
	//					  編集が出来ない場合(出力中等)に失敗します
	bool (*call_edit_section)(void (*func_proc_edit)(EDIT_SECTION* edit));

	// call_edit_section()に引数paramを渡せるようにした関数です
	// param			: 任意のユーザーデータのポインタ
	bool (*call_edit_section_param)(void* param, void (*func_proc_edit)(void* param, EDIT_SECTION* edit));

	// 編集情報を取得します
	// 編集情報を排他制御する為に参照ロックします。※同一スレッドで既にロック状態の場合はそのまま取得します。
	// info			: 編集情報の格納先へのポインタ
	// info_size	: 編集情報の格納先のサイズ ※EDIT_INFOと異なる場合はサイズ分のみ取得されます
	void (*get_edit_info)(EDIT_INFO* info, int info_size);

	// ホストアプリケーションを再起動します
	void (*restart_host_app)();

	// エフェクト名の一覧をコールバック関数(func_proc_enum_effect)で取得します
	// param					: 任意のユーザーデータのポインタ
	// func_proc_enum_effect	: エフェクト名の取得処理のコールバック関数
	void (*enum_effect_name)(void* param, void (*func_proc_enum_effect)(void* param, LPCWSTR name, int type, int flag));
	// エフェクト種別 ※今後追加される可能性があります
	static constexpr int EFFECT_TYPE_FILTER		= 1;	// フィルタ効果
	static constexpr int EFFECT_TYPE_INPUT		= 2;	// メディア入力
	static constexpr int EFFECT_TYPE_TRANSITION = 3;	// シーンチェンジ
	// エフェクトフラグ ※今後追加される可能性があります
	static constexpr int EFFECT_FLAG_VIDEO		= 1;	// 画像をサポート
	static constexpr int EFFECT_FLAG_AUDIO		= 2;	// 音声をサポート
	static constexpr int EFFECT_FLAG_FILTER		= 4;	// フィルタオブジェクトをサポート

	// モジュール情報の一覧をコールバック関数(func_proc_enum_module)で取得します
	// param					: 任意のユーザーデータのポインタ
	// func_proc_enum_module	: モジュール情報の取得処理のコールバック関数
	void (*enum_module_info)(void* param, void (*func_proc_enum_module)(void* param, MODULE_INFO* info));

	// ホストアプリケーションのメインウィンドウのハンドルを取得します
	HWND (*get_host_app_window)();

};

//----------------------------------------------------------------------------------

// プロジェクトファイル構造体
// プロジェクトファイルのロード、セーブのコールバックや編集のコールバック関数内で利用出来ます
// プロジェクトの保存データはプラグイン毎のデータ領域になります
struct PROJECT_FILE {
	// プロジェクトに保存されている文字列(UTF-8)を取得します
	// key		: キー名(UTF-8)
	// 戻り値	: 取得した文字列へのポインタ (未設定の場合はnullptr)
	//			  ※コールバック処理の終了まで有効
	LPCSTR (*get_param_string)(LPCSTR key);

	// プロジェクトに文字列(UTF-8)を保存します
	// key		: キー名(UTF-8)
	// value	: 保存する文字列(UTF-8)
	void (*set_param_string)(LPCSTR key, LPCSTR value);

	// プロジェクトに保存されているバイナリデータを取得します
	// key		: キー名(UTF-8)
	// data		: 取得するデータの格納先へのポインタ
	// size		: 取得するデータのサイズ (保存されているサイズと異なる場合は失敗します)
	// 戻り値	: 正しく取得出来た場合はtrue
	bool (*get_param_binary)(LPCSTR key, void* data, int size);

	// プロジェクトにバイナリデータを保存します
	// key		: キー名(UTF-8)
	// data		: 保存するデータへのポインタ
	// size		: 保存するデータのサイズ (4096バイト以下)
	void (*set_param_binary)(LPCSTR key, void* data, int size);

	// プロジェクトに保存されているデータを全て削除します
	void (*clear_params)();

	// プロジェクトファイルのパスを取得します
	// key		: キー名(UTF-8)
	// 戻り値	: プロジェクトファイルパスへのポインタ (ファイルパスは未設定の場合があります)
	//			  ※コールバック処理の終了まで有効
	LPCWSTR (*get_project_file_path)();

};

//----------------------------------------------------------------------------------

// ホストアプリケーション構造体
struct HOST_APP_TABLE {
	// プラグインの情報を設定する
	// information	: プラグインの情報
	void (*set_plugin_information)(LPCWSTR information);

	// 入力プラグインを登録する
	// input_plugin_table	: 入力プラグイン構造体
	void (*register_input_plugin)(INPUT_PLUGIN_TABLE* input_plugin_table);

	// 出力プラグインを登録する
	// output_plugin_table	: 出力プラグイン構造体
	void (*register_output_plugin)(OUTPUT_PLUGIN_TABLE* output_plugin_table);

	// フィルタプラグインを登録する
	// filter_plugin_table	: フィルタプラグイン構造体
	void (*register_filter_plugin)(FILTER_PLUGIN_TABLE* filter_plugin_table);

	// スクリプトモジュールを登録する
	// script_module_table	: スクリプトモジュール構造体
	void (*register_script_module)(SCRIPT_MODULE_TABLE* script_module_table);

	// インポートメニューを登録する (ウィンドウメニューのファイルに追加されます)
	// name				: インポートメニューの名称
	// func_proc_import	: インポートメニュー選択時のコールバック関数
	void (*register_import_menu)(LPCWSTR name, void (*func_proc_import)(EDIT_SECTION* edit));

	// エクスポートメニューを登録する (ウィンドウメニューのファイルに追加されます)
	// name				: エクスポートメニューの名称
	// func_proc_export	: エクスポートメニュー選択時のコールバック関数
	void (*register_export_menu)(LPCWSTR name, void (*func_proc_export)(EDIT_SECTION* edit));

	// ウィンドウクライアントを登録する
	// name		: ウィンドウの名称
	// hwnd		: ウィンドウハンドル
	// ウィンドウにはWS_CHILDが追加され親ウィンドウが設定されます ※WS_POPUPは削除されます
	void (*register_window_client)(LPCWSTR name, HWND hwnd);

	// プロジェクトデータ編集用のハンドルを取得します
	// 戻り値	: 編集ハンドル
	EDIT_HANDLE* (*create_edit_handle)();

	// プロジェクトファイルをロードした直後に呼ばれる関数を登録する ※プロジェクトの初期化時にも呼ばれます
	// func_project_load	: プロジェクトファイルのロード時のコールバック関数
	void (*register_project_load_handler)(void (*func_project_load)(PROJECT_FILE* project));

	// プロジェクトファイルをセーブする直前に呼ばれる関数を登録する
	// func_project_save	: プロジェクトファイルのセーブ時のコールバック関数
	void (*register_project_save_handler)(void (*func_project_save)(PROJECT_FILE* project));

	// レイヤーメニューを登録する (レイヤー編集でオブジェクト未選択時の右クリックメニューに追加されます)
	// name					: レイヤーメニューの名称
	// func_proc_layer_menu	: レイヤーメニュー選択時のコールバック関数
	void (*register_layer_menu)(LPCWSTR name, void (*func_proc_layer_menu)(EDIT_SECTION* edit));

	// オブジェクトメニューを登録する (レイヤー編集でオブジェクト選択時の右クリックメニューに追加されます)
	// name						: オブジェクトメニューの名称
	// func_proc_object_menu	: オブジェクトメニュー選択時のコールバック関数
	void (*register_object_menu)(LPCWSTR name, void (*func_proc_object_menu)(EDIT_SECTION* edit));

	// 設定メニューを登録する
	// 設定メニューの登録後にウィンドウクライアントを登録するとシステムメニューに「設定」が追加されます
	// name				: 設定メニューの名称
	// func_config		: 設定メニュー選択時のコールバック関数
	void (*register_config_menu)(LPCWSTR name, void (*func_config)(HWND hwnd, HINSTANCE dll_hinst));

	// 編集メニューを登録する
	// name					: 編集メニューの名称 ※名称に'\'を入れると表示を階層に出来ます
	// func_proc_edit_menu	: 編集メニュー選択時のコールバック関数
	void (*register_edit_menu)(LPCWSTR name, void (*func_proc_edit_menu)(EDIT_SECTION* edit));

	// キャッシュを破棄の操作時に呼ばれる関数を登録する
	// func_proc_clear_cache	: キャッシュの破棄時のコールバック関数
	void (*register_clear_cache_handler)(void (*func_proc_clear_cache)(EDIT_SECTION* edit));

	// シーンを変更した直後に呼ばれる関数を登録する ※シーンの設定情報が更新された時にも呼ばれます
	// func_proc_change_scene	: シーン変更時のコールバック関数
	void (*register_change_scene_handler)(void (*func_proc_change_scene)(EDIT_SECTION* edit));

	// インポートメニューを登録する (ウィンドウメニューのファイルに追加されます)
	// 引数paramを渡して編集セクションにしないでコールバックを呼び出します
	// name				: インポートメニューの名称
	// param			: 任意のユーザーデータのポインタ
	// func_proc_import	: インポートメニュー選択時のコールバック関数
	void (*register_import_menu_param)(LPCWSTR name, void* param, void (*func_proc_import)(void* param));

	// エクスポートメニューを登録する (ウィンドウメニューのファイルに追加されます)
	// 引数paramを渡して編集セクションにしないでコールバックを呼び出します
	// name				: エクスポートメニューの名称
	// param			: 任意のユーザーデータのポインタ
	// func_proc_export	: エクスポートメニュー選択時のコールバック関数
	void (*register_export_menu_param)(LPCWSTR name, void* param, void (*func_proc_export)(void* param));

	// レイヤーメニューを登録する (レイヤー編集でオブジェクト未選択時の右クリックメニューに追加されます)
	// 引数paramを渡して編集セクションにしないでコールバックを呼び出します
	// name					: レイヤーメニューの名称
	// param				: 任意のユーザーデータのポインタ
	// func_proc_layer_menu	: レイヤーメニュー選択時のコールバック関数
	void (*register_layer_menu_param)(LPCWSTR name, void* param, void (*func_proc_layer_menu)(void* param));

	// オブジェクトメニューを登録する (レイヤー編集でオブジェクト選択時の右クリックメニューに追加されます)
	// 引数paramを渡して編集セクションにしないでコールバックを呼び出します
	// name						: オブジェクトメニューの名称
	// param					: 任意のユーザーデータのポインタ
	// func_proc_object_menu	: オブジェクトメニュー選択時のコールバック関数
	void (*register_object_menu_param)(LPCWSTR name, void* param, void (*func_proc_object_menu)(void* param));

	// 編集メニューを登録する
	// 引数paramを渡して編集セクションにしないでコールバックを呼び出します
	// name					: 編集メニューの名称 ※名称に'\'を入れると表示を階層に出来ます
	// param				: 任意のユーザーデータのポインタ
	// func_proc_edit_menu	: 編集メニュー選択時のコールバック関数
	void (*register_edit_menu_param)(LPCWSTR name, void* param, void (*func_proc_edit_menu)(void* param));

};
