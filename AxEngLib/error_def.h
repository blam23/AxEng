// make sure to #undef ERRORS after including this file anywhere.
// in fact, just use error.h directly instead!

#define ERRORS \
	ERR(Success,                       0) \
	ERR(GenericFailure,                1) \
	ERR(InvalidConfiguration,          2) \
	ERR(IO,                            3) \
	ERR(Lua,                           4) \
	ERR(InvalidTexture,                5) \
	ERR(ApplicationLoadFailed,      1000) \
