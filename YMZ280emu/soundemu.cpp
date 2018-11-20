#include "stdafx.h"
#include "soundemu.h"
#include "ymz280b.h"

#define SAMPLE_BUFFER_SIZE	441

stream_sample_t* _lbuf = NULL;
stream_sample_t* _rbuf = NULL;
stream_sample_t* stream_buffer[2] = { _lbuf, _rbuf, };

virtual_sound_chip_t* _vsc = NULL;

#ifdef _WIN32

#ifdef _DEBUG
#define DEBUG(x)	OutputDebugString(x)
#else
#define DEBUG(x)	{x;}
#endif


#define MEMORY_SIZE	16777216

// マクロ
#define SAFE_RELEASE(x)		{ if( x ) { x->Release(); x=NULL; } }
#define SAFE_FREE(x)		{ if( x ) { free(x); x=NULL; } }


//////////////////////////////////////////////////////////////////////////////////////
// WASAPI関連
//////////////////////////////////////////////////////////////////////////////////////

// ヘッダ
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <FunctionDiscoveryKeys_devpkey.h>

// ライブラリ
#pragma comment(lib, "Avrt.lib")
#pragma comment(lib, "winmm.lib")

// インターフェースのGUIDの実体(プロジェクト内のCファイルに必ず1つ必要)
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioClock = __uuidof(IAudioClock);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

IMMDeviceEnumerator		*pDeviceEnumerator = NULL;		// マルチメディアデバイス列挙インターフェース
IMMDevice				*pDevice = NULL;		// デバイスインターフェース
IAudioClient			*pAudioClient = NULL;		// オーディオクライアントインターフェース
IAudioRenderClient		*pRenderClient = NULL;		// レンダークライアントインターフェース
int						iFrameSize = 0;		// 1サンプル分のバッファサイズ

HANDLE					hEvent = NULL;		// イベントハンドル
HANDLE					hThread = NULL;		// スレッドハンドル
BOOL					bThreadLoop = FALSE;	// スレッド処理中か


//////////////////////////////////////////////////////////////////////////////////////
// 再生スレッド
//////////////////////////////////////////////////////////////////////////////////////
DWORD CALLBACK PlayThread(LPVOID param)
{

	while (bThreadLoop) {
		// 次のバッファ取得が必要になるまで待機
		DWORD retval = WaitForSingleObject(hEvent, 2000);
		if (retval != WAIT_OBJECT_0) {
			pAudioClient->Stop();
			break;
		}

		// 今回必要なフレーム数を取得
		UINT32 frame_count;
		HRESULT ret = pAudioClient->GetBufferSize(&frame_count);

		// フレーム数からバッファサイズを算出
		int buf_size = frame_count * iFrameSize;

		uint32_t actual_count = min(frame_count, SAMPLE_BUFFER_SIZE);
		if (frame_count > SAMPLE_BUFFER_SIZE) {
		}
		_vsc->generate_sample(stream_buffer, actual_count);
		// 出力バッファのポインタを取得
		LPBYTE dst;
		ret = pRenderClient->GetBuffer(actual_count, &dst);
		if (SUCCEEDED(ret)) {
			int16_t* pdstbuf = (int16_t*)dst;
			for (uint32_t i = 0; i < actual_count; i++) {
				*(pdstbuf++) = (int16_t)(_lbuf[i]);
				*(pdstbuf++) = (int16_t)(_rbuf[i]);
			}
			// バッファを開放
			pRenderClient->ReleaseBuffer(actual_count, 0);
		}

	}
	return 0;
}


//////////////////////////////////////////////////////////////////////////////////////
// WASAPIの初期化
//////////////////////////////////////////////////////////////////////////////////////
BOOL InitWasapi(int latency)
{
	HRESULT ret;

	// マルチメディアデバイス列挙子
	ret = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pDeviceEnumerator);
	if (FAILED(ret)) {
		return FALSE;
	}

	// デフォルトのデバイスを選択
	ret = pDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	if (FAILED(ret)) {
		return FALSE;
	}

	// オーディオクライアント
	ret = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	if (FAILED(ret)) {
		return FALSE;
	}

	// フォーマットの構築
	WAVEFORMATEXTENSIBLE wf;
	ZeroMemory(&wf, sizeof(wf));
	wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE);
	wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wf.Format.nChannels = 2;
	wf.Format.nSamplesPerSec = 44100;
	wf.Format.wBitsPerSample = 16;
	wf.Format.nBlockAlign = wf.Format.nChannels * wf.Format.wBitsPerSample / 8;
	wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
	wf.Samples.wValidBitsPerSample = wf.Format.wBitsPerSample;
	wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
	wf.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

	// 1サンプルのサイズを保存(16bitステレオなら4byte)
	iFrameSize = wf.Format.nBlockAlign;

	// フォーマットのサポートチェック
	ret = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&wf, NULL);
	if (FAILED(ret)) {
		return FALSE;
	}

	// レイテンシ設定
	REFERENCE_TIME default_device_period = 0;
	REFERENCE_TIME minimum_device_period = 0;

	if (latency != 0) {
		default_device_period = (REFERENCE_TIME)latency * 10000LL;		// デフォルトデバイスピリオドとしてセット
	}
	else {
		ret = pAudioClient->GetDevicePeriod(&default_device_period, &minimum_device_period);
	}

	// 初期化
	UINT32 frame = 0;
	ret = pAudioClient->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
		AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
		default_device_period,				// デフォルトデバイスピリオド値をセット
		default_device_period,				// デフォルトデバイスピリオド値をセット
		(WAVEFORMATEX*)&wf,
		NULL);
	if (FAILED(ret)) {
		if (ret == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {

			// 修正後のフレーム数を取得
			ret = pAudioClient->GetBufferSize(&frame);
			default_device_period = (REFERENCE_TIME)(10000.0 *						// (REFERENCE_TIME(100ns) / ms) *
				1000 *						// (ms / s) *
				frame /						// frames /
				wf.Format.nSamplesPerSec +	// (frames / s)
				0.5);							// 四捨五入？

			// 一度破棄してオーディオクライアントを再生成
			SAFE_RELEASE(pAudioClient);
			ret = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
			if (FAILED(ret)) {
				return FALSE;
			}

			// 再挑戦
			ret = pAudioClient->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
				AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
				default_device_period,
				default_device_period,
				(WAVEFORMATEX*)&wf,
				NULL);
		}

		if (FAILED(ret)) {
			switch (ret)
			{
			case AUDCLNT_E_NOT_INITIALIZED:					DEBUG("AUDCLNT_E_NOT_INITIALIZED\n");					break;
			case AUDCLNT_E_ALREADY_INITIALIZED:				DEBUG("AUDCLNT_E_ALREADY_INITIALIZED\n");				break;
			case AUDCLNT_E_WRONG_ENDPOINT_TYPE:				DEBUG("AUDCLNT_E_WRONG_ENDPOINT_TYPE\n");				break;
			case AUDCLNT_E_DEVICE_INVALIDATED:				DEBUG("AUDCLNT_E_DEVICE_INVALIDATED\n");				break;
			case AUDCLNT_E_NOT_STOPPED:						DEBUG("AUDCLNT_E_NOT_STOPPED\n");						break;
			case AUDCLNT_E_BUFFER_TOO_LARGE:				DEBUG("AUDCLNT_E_BUFFER_TOO_LARGE\n");				break;
			case AUDCLNT_E_OUT_OF_ORDER:					DEBUG("AUDCLNT_E_OUT_OF_ORDER\n");					break;
			case AUDCLNT_E_UNSUPPORTED_FORMAT:				DEBUG("AUDCLNT_E_UNSUPPORTED_FORMAT\n");				break;
			case AUDCLNT_E_INVALID_SIZE:					DEBUG("AUDCLNT_E_INVALID_SIZE\n");					break;
			case AUDCLNT_E_DEVICE_IN_USE:					DEBUG("AUDCLNT_E_DEVICE_IN_USE\n");					break;
			case AUDCLNT_E_BUFFER_OPERATION_PENDING:		DEBUG("AUDCLNT_E_BUFFER_OPERATION_PENDING\n");		break;
			case AUDCLNT_E_THREAD_NOT_REGISTERED:			DEBUG("AUDCLNT_E_THREAD_NOT_REGISTERED\n");			break;
				//			case AUDCLNT_E_NO_SINGLE_PROCESS:				DEBUG( "AUDCLNT_E_NO_SINGLE_PROCESS\n" );				break;	// VC2010では未定義？
			case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED:		DEBUG("AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED\n");		break;
			case AUDCLNT_E_ENDPOINT_CREATE_FAILED:			DEBUG("AUDCLNT_E_ENDPOINT_CREATE_FAILED\n");			break;
			case AUDCLNT_E_SERVICE_NOT_RUNNING:				DEBUG("AUDCLNT_E_SERVICE_NOT_RUNNING\n");				break;
			case AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED:		DEBUG("AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED\n");		break;
			case AUDCLNT_E_EXCLUSIVE_MODE_ONLY:				DEBUG("AUDCLNT_E_EXCLUSIVE_MODE_ONLY\n");				break;
			case AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL:	DEBUG("AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL\n");	break;
			case AUDCLNT_E_EVENTHANDLE_NOT_SET:				DEBUG("AUDCLNT_E_EVENTHANDLE_NOT_SET\n");				break;
			case AUDCLNT_E_INCORRECT_BUFFER_SIZE:			DEBUG("AUDCLNT_E_INCORRECT_BUFFER_SIZE\n");			break;
			case AUDCLNT_E_BUFFER_SIZE_ERROR:				DEBUG("AUDCLNT_E_BUFFER_SIZE_ERROR\n");				break;
			case AUDCLNT_E_CPUUSAGE_EXCEEDED:				DEBUG("AUDCLNT_E_CPUUSAGE_EXCEEDED\n");				break;
			default:										DEBUG("UNKNOWN\n");									break;
			}
			return FALSE;
		}
	}

	// イベント生成
	hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!hEvent) {
		return FALSE;
	}

	// イベントのセット
	ret = pAudioClient->SetEventHandle(hEvent);
	if (FAILED(ret)) {
		return FALSE;
	}

	// レンダラーの取得
	ret = pAudioClient->GetService(IID_IAudioRenderClient, (void**)&pRenderClient);
	if (FAILED(ret)) {
		return FALSE;
	}

	// WASAPI情報取得
	ret = pAudioClient->GetBufferSize(&frame);

	UINT32 size = frame * iFrameSize;

	// ゼロクリアをしてイベントをリセット
	LPBYTE pData;
	ret = pRenderClient->GetBuffer(frame, &pData);
	if (SUCCEEDED(ret)) {
		ZeroMemory(pData, size);
		pRenderClient->ReleaseBuffer(frame, 0);
	}

	// スレッドループフラグを立てる
	bThreadLoop = TRUE;

	// 再生スレッド起動
	DWORD dwThread;
	hThread = CreateThread(NULL, 0, PlayThread, NULL, 0, &dwThread);
	if (!hThread) {
		// 失敗
		return FALSE;
	}

	// 再生開始
	pAudioClient->Start();

	DEBUG("WASAPI初期化完了\n");
	return TRUE;
}


//////////////////////////////////////////////////////////////////////////////////////
// WASAPIの終了
//////////////////////////////////////////////////////////////////////////////////////
void ExitWasapi()
{
	// スレッドループフラグを落とす
	bThreadLoop = FALSE;

	// スレッド終了処理
	if (hThread) {
		// スレッドが完全に終了するまで待機
		WaitForSingleObject(hThread, INFINITE);
		CloseHandle(hThread);
		hThread = NULL;
	}

	// イベントを開放処理
	if (hEvent) {
		CloseHandle(hEvent);
		hEvent = NULL;
	}

	// インターフェース開放
	SAFE_RELEASE(pRenderClient);
	SAFE_RELEASE(pAudioClient);
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pDeviceEnumerator);

	DEBUG("WASAPI終了\n");
}



uint8_t* device_memory = NULL;

void init_emu()
{
	device_memory = new uint8_t[MEMORY_SIZE];
	_lbuf = new stream_sample_t[SAMPLE_BUFFER_SIZE];
	_rbuf = new stream_sample_t[SAMPLE_BUFFER_SIZE];
	_vsc = new ymz280b_device(44100 * 384);	//16.9344MHz
	if (_vsc) {
		_vsc->device_start();
		_vsc->device_reset();
	}
}

uint8_t read_byte(uint32_t address)
{
	address &= (MEMORY_SIZE - 1);
	return (device_memory != NULL) ? device_memory[address] : 0xff;
}

void write_byte(uint32_t address, uint8_t data)
{
	address &= (MEMORY_SIZE - 1);
	if (device_memory) {
		device_memory[address] = data;
	}
}

#endif