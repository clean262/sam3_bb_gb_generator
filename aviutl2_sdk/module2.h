//----------------------------------------------------------------------------------
//	スクリプトモジュール ヘッダーファイル for AviUtl ExEdit2
//	By ＫＥＮくん
//----------------------------------------------------------------------------------

//	スクリプトモジュールは下記の関数を外部公開すると呼び出されます
//
//	スクリプトモジュール構造体のポインタを渡す関数 (必須)
//		SCRIPT_MODULE_TABLE* GetScriptModuleTable(void)
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

// スクリプトモジュール引数構造体
struct SCRIPT_MODULE_PARAM {
	// 引数の数を取得する
	// 戻り値		: 引数の数
	int (*get_param_num)();

	// 引数を整数で取得する
	// index		: 引数の位置(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合は0)
	int (*get_param_int)(int index);

	// 引数を浮動小数点で取得する
	// index		: 引数の位置(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合は0)
	double (*get_param_double)(int index);

	// 引数を文字列(UTF-8)で取得する
	// index		: 引数の位置(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合はnullptr)
	LPCSTR (*get_param_string)(int index);

	// 引数をデータのポインタで取得する
	// index		: 引数の位置(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合はnullptr)
	void* (*get_param_data)(int index);

	//--------------------------------

	// 引数の連想配列要素を整数で取得する
	// index		: 引数の位置(0〜)
	// key			: キー名(UTF-8)
	// 戻り値		: 引数の値 (取得出来ない場合は0)
	int (*get_param_table_int)(int index, LPCSTR key);

	// 引数の連想配列要素を浮動小数点で取得する
	// index		: 引数の位置(0〜)
	// key			: キー名(UTF-8)
	// 戻り値		: 引数の値 (取得出来ない場合は0)
	double (*get_param_table_double)(int index, LPCSTR key);

	// 引数の連想配列要素を文字列(UTF-8)で取得する
	// index		: 引数の位置(0〜)
	// key			: キー名(UTF-8)
	// 戻り値		: 引数の値 (取得出来ない場合はnullptr)
	LPCSTR (*get_param_table_string)(int index, LPCSTR key);

	//--------------------------------

	// 引数の配列要素の数を取得する
	// index		: 引数の位置(0〜)
	// 戻り値		: 配列要素の数
	int (*get_param_array_num)(int index);

	// 引数の配列要素を整数で取得する
	// index		: 引数の位置(0〜)
	// key			: 配列のインデックス(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合は0)
	int (*get_param_array_int)(int index, int key);

	// 引数の配列要素を浮動小数点で取得する
	// index		: 引数の位置(0〜)
	// key			: 配列のインデックス(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合は0)
	double (*get_param_array_double)(int index, int key);

	// 引数の配列要素を文字列(UTF-8)で取得する
	// index		: 引数の位置(0〜)
	// key			: 配列のインデックス(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合はnullptr)
	LPCSTR (*get_param_array_string)(int index, int key);

	//--------------------------------

	// 整数の戻り値を追加する
	// value		: 戻り値
	void (*push_result_int)(int value);

	// 浮動小数点の戻り値を追加する
	// value		: 戻り値
	void (*push_result_double)(double value);

	// 文字列(UTF-8)の戻り値を追加する
	// value		: 戻り値
	void (*push_result_string)(LPCSTR value);

	// データのポインタの戻り値を追加する
	// value		: 戻り値
	void (*push_result_data)(void* value);

	//--------------------------------

	// 整数連想配列の戻り値を追加する
	// key			: キー名(UTF-8)の配列
	// value		: 戻り値の配列
	// num			: 配列の要素数
	void (*push_result_table_int)(LPCSTR* key, int* value, int num);

	// 浮動小数点連想配列の戻り値を追加する
	// key			: キー名(UTF-8)の配列
	// value		: 戻り値の配列
	// num			: 配列の要素数
	void (*push_result_table_double)(LPCSTR* key, double* value, int num);

	// 文字列(UTF-8)連想配列の戻り値を追加する
	// key			: キー名(UTF-8)の配列
	// value		: 戻り値の配列
	// num			: 配列の要素数
	void (*push_result_table_string)(LPCSTR* key, LPCSTR* value, int num);

	//--------------------------------

	// 整数配列の戻り値を追加する
	// value		: 戻り値の配列
	// num			: 配列の要素数
	void (*push_result_array_int)(int* value, int num);

	// 浮動小数点配列の戻り値を追加する
	// value		: 戻り値の配列
	// num			: 配列の要素数
	void (*push_result_array_double)(double* value, int num);

	// 文字列(UTF-8)配列の戻り値を追加する
	// value		: 戻り値の配列
	// num			: 配列の要素数
	void (*push_result_array_string)(LPCSTR *value, int num);

	//--------------------------------

	// エラーメッセージを設定する
	// 呼び出された関数をエラー終了する場合に設定します
	// message		: エラーメッセージ(UTF-8)
	void (*set_error)(LPCSTR message);

	//--------------------------------

	// 引数をブール値で取得する
	// index		: 引数の位置(0〜)
	// 戻り値		: 引数の値 (取得出来ない場合はfalse)
	bool (*get_param_boolean)(int index);

	// ブール値の戻り値を追加する
	// value		: 戻り値
	void (*push_result_boolean)(bool value);

	// 引数の連想配列要素をブール値で取得する
	// index		: 引数の位置(0〜)
	// key			: キー名(UTF-8)
	// 戻り値		: 引数の値 (取得出来ない場合はfalse)
	bool (*get_param_table_boolean)(int index, LPCSTR key);

};

//----------------------------------------------------------------------------------

// スクリプトモジュール関数定義構造体
struct SCRIPT_MODULE_FUNCTION {
	LPCWSTR name;						// 関数名
	void (*func)(SCRIPT_MODULE_PARAM*);	// 関数へのポインタ
};

// スクリプトモジュール構造体
struct SCRIPT_MODULE_TABLE {
	LPCWSTR information;				// スクリプトモジュールの情報
	SCRIPT_MODULE_FUNCTION* functions;	// 登録する関数の一覧 (SCRIPT_MODULE_FUNCTIONを列挙して関数名がnullの要素で終端したリストへのポインタ)
};

