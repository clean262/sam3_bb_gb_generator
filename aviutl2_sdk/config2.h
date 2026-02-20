//----------------------------------------------------------------------------------
//	設定関連機能 ヘッダーファイル for AviUtl ExEdit2
//	By ＫＥＮくん
//----------------------------------------------------------------------------------

//	各種プラグインで下記の関数を外部公開すると呼び出されます
// 
//	設定関連機能初期化関数
//		void InitializeConfig(CONFIG_HANDLE* config)
//		※InitializePlugin()より先に呼ばれます

//----------------------------------------------------------------------------------

// フォント情報構造体
struct FONT_INFO {
	LPCWSTR name;	// フォント名
	float size;		// フォントサイズ
};

// 設定ハンドル
struct CONFIG_HANDLE {
	// アプリケーションデータフォルダのパス
	LPCWSTR app_data_path;

	// 現在の言語設定で定義されているテキストを取得します
	// 参照する言語設定のセクションはInitializeConfig()を定義したプラグインのファイル名になります
	// text		: 元のテキスト(.aul2ファイルのキー名)
	// 戻り値	: 定義されているテキストへのポインタ (未定義の場合は引数のテキストのポインタが返却されます)
	//			  ※言語設定が更新されるまで有効
	LPCWSTR (*translate)(CONFIG_HANDLE* handle, LPCWSTR text);

	// 現在の言語設定で定義されているテキストを取得します ※任意のセクションから取得出来ます
	// section	: 言語設定のセクション(.aul2ファイルのセクション名)
	// text		: 元のテキスト(.aul2ファイルのキー名)
	// 戻り値	: 定義されているテキストへのポインタ (未定義の場合は引数のテキストのポインタが返却されます)
	//			  ※言語設定が更新されるまで有効
	LPCWSTR (*get_language_text)(CONFIG_HANDLE* handle, LPCWSTR section, LPCWSTR text);

	// 設定ファイルで定義されているフォント情報を取得します　
	// key		: 設定ファイル(style.conf)の[Font]のキー名
	// 戻り値	: フォント情報構造体へのポインタ (取得出来ない場合はデフォルトのフォントが返却されます)
	//			  ※次にこの関数を呼び出すまで有効
	FONT_INFO* (*get_font_info)(CONFIG_HANDLE* handle, LPCSTR key);

	// 設定ファイルで定義されている色コードを取得します ※複数色の場合は最初の色が取得されます
	// key		: 設定ファイル(style.conf)の[Color]のキー名
	// 戻り値	: 定義されている色コードの値 (取得出来ない場合は0が返却されます)
	int (*get_color_code)(CONFIG_HANDLE* handle, LPCSTR key);

	// 設定ファイルで定義されているレイアウトサイズを取得します
	// key		: 設定ファイル(style.conf)の[Layout]のキー名
	// 戻り値	: 定義されているサイズ (取得出来ない場合は0が返却されます)
	int (*get_layout_size)(CONFIG_HANDLE* handle, LPCSTR key);

	// 設定ファイルで定義されている色コードを取得します
	// key		: 設定ファイル(style.conf)の[Color]のキー名
	// index	: 取得する色のインデックス (-1を指定すると色の数を返却します)
	// 戻り値	: 定義されている色コードの値 (取得出来ない場合は0が返却されます)
	int (*get_color_code_index)(CONFIG_HANDLE* handle, LPCSTR key, int index);

};
