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

// �}�N��
#define SAFE_RELEASE(x)		{ if( x ) { x->Release(); x=NULL; } }
#define SAFE_FREE(x)		{ if( x ) { free(x); x=NULL; } }


//////////////////////////////////////////////////////////////////////////////////////
// WASAPI�֘A
//////////////////////////////////////////////////////////////////////////////////////

// �w�b�_
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <FunctionDiscoveryKeys_devpkey.h>

// ���C�u����
#pragma comment(lib, "Avrt.lib")
#pragma comment(lib, "winmm.lib")

// �C���^�[�t�F�[�X��GUID�̎���(�v���W�F�N�g����C�t�@�C���ɕK��1�K�v)
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioClock = __uuidof(IAudioClock);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

IMMDeviceEnumerator		*pDeviceEnumerator = NULL;		// �}���`���f�B�A�f�o�C�X�񋓃C���^�[�t�F�[�X
IMMDevice				*pDevice = NULL;		// �f�o�C�X�C���^�[�t�F�[�X
IAudioClient			*pAudioClient = NULL;		// �I�[�f�B�I�N���C�A���g�C���^�[�t�F�[�X
IAudioRenderClient		*pRenderClient = NULL;		// �����_�[�N���C�A���g�C���^�[�t�F�[�X
int						iFrameSize = 0;		// 1�T���v�����̃o�b�t�@�T�C�Y

HANDLE					hEvent = NULL;		// �C�x���g�n���h��
HANDLE					hThread = NULL;		// �X���b�h�n���h��
BOOL					bThreadLoop = FALSE;	// �X���b�h��������


//////////////////////////////////////////////////////////////////////////////////////
// �Đ��X���b�h
//////////////////////////////////////////////////////////////////////////////////////
DWORD CALLBACK PlayThread(LPVOID param)
{

	while (bThreadLoop) {
		// ���̃o�b�t�@�擾���K�v�ɂȂ�܂őҋ@
		DWORD retval = WaitForSingleObject(hEvent, 2000);
		if (retval != WAIT_OBJECT_0) {
			pAudioClient->Stop();
			break;
		}

		// ����K�v�ȃt���[�������擾
		UINT32 frame_count;
		HRESULT ret = pAudioClient->GetBufferSize(&frame_count);

		// �t���[��������o�b�t�@�T�C�Y���Z�o
		int buf_size = frame_count * iFrameSize;

		uint32_t actual_count = min(frame_count, SAMPLE_BUFFER_SIZE);
		if (frame_count > SAMPLE_BUFFER_SIZE) {
		}
		_vsc->generate_sample(stream_buffer, actual_count);
		// �o�̓o�b�t�@�̃|�C���^���擾
		LPBYTE dst;
		ret = pRenderClient->GetBuffer(actual_count, &dst);
		if (SUCCEEDED(ret)) {
			int16_t* pdstbuf = (int16_t*)dst;
			for (uint32_t i = 0; i < actual_count; i++) {
				*(pdstbuf++) = (int16_t)(_lbuf[i]);
				*(pdstbuf++) = (int16_t)(_rbuf[i]);
			}
			// �o�b�t�@���J��
			pRenderClient->ReleaseBuffer(actual_count, 0);
		}

	}
	return 0;
}


//////////////////////////////////////////////////////////////////////////////////////
// WASAPI�̏�����
//////////////////////////////////////////////////////////////////////////////////////
BOOL InitWasapi(int latency)
{
	HRESULT ret;

	// �}���`���f�B�A�f�o�C�X�񋓎q
	ret = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pDeviceEnumerator);
	if (FAILED(ret)) {
		return FALSE;
	}

	// �f�t�H���g�̃f�o�C�X��I��
	ret = pDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	if (FAILED(ret)) {
		return FALSE;
	}

	// �I�[�f�B�I�N���C�A���g
	ret = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	if (FAILED(ret)) {
		return FALSE;
	}

	// �t�H�[�}�b�g�̍\�z
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

	// 1�T���v���̃T�C�Y��ۑ�(16bit�X�e���I�Ȃ�4byte)
	iFrameSize = wf.Format.nBlockAlign;

	// �t�H�[�}�b�g�̃T�|�[�g�`�F�b�N
	ret = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&wf, NULL);
	if (FAILED(ret)) {
		return FALSE;
	}

	// ���C�e���V�ݒ�
	REFERENCE_TIME default_device_period = 0;
	REFERENCE_TIME minimum_device_period = 0;

	if (latency != 0) {
		default_device_period = (REFERENCE_TIME)latency * 10000LL;		// �f�t�H���g�f�o�C�X�s���I�h�Ƃ��ăZ�b�g
	}
	else {
		ret = pAudioClient->GetDevicePeriod(&default_device_period, &minimum_device_period);
	}

	// ������
	UINT32 frame = 0;
	ret = pAudioClient->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
		AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
		default_device_period,				// �f�t�H���g�f�o�C�X�s���I�h�l���Z�b�g
		default_device_period,				// �f�t�H���g�f�o�C�X�s���I�h�l���Z�b�g
		(WAVEFORMATEX*)&wf,
		NULL);
	if (FAILED(ret)) {
		if (ret == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {

			// �C����̃t���[�������擾
			ret = pAudioClient->GetBufferSize(&frame);
			default_device_period = (REFERENCE_TIME)(10000.0 *						// (REFERENCE_TIME(100ns) / ms) *
				1000 *						// (ms / s) *
				frame /						// frames /
				wf.Format.nSamplesPerSec +	// (frames / s)
				0.5);							// �l�̌ܓ��H

			// ��x�j�����ăI�[�f�B�I�N���C�A���g���Đ���
			SAFE_RELEASE(pAudioClient);
			ret = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
			if (FAILED(ret)) {
				return FALSE;
			}

			// �Ē���
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
				//			case AUDCLNT_E_NO_SINGLE_PROCESS:				DEBUG( "AUDCLNT_E_NO_SINGLE_PROCESS\n" );				break;	// VC2010�ł͖���`�H
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

	// �C�x���g����
	hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!hEvent) {
		return FALSE;
	}

	// �C�x���g�̃Z�b�g
	ret = pAudioClient->SetEventHandle(hEvent);
	if (FAILED(ret)) {
		return FALSE;
	}

	// �����_���[�̎擾
	ret = pAudioClient->GetService(IID_IAudioRenderClient, (void**)&pRenderClient);
	if (FAILED(ret)) {
		return FALSE;
	}

	// WASAPI���擾
	ret = pAudioClient->GetBufferSize(&frame);

	UINT32 size = frame * iFrameSize;

	// �[���N���A�����ăC�x���g�����Z�b�g
	LPBYTE pData;
	ret = pRenderClient->GetBuffer(frame, &pData);
	if (SUCCEEDED(ret)) {
		ZeroMemory(pData, size);
		pRenderClient->ReleaseBuffer(frame, 0);
	}

	// �X���b�h���[�v�t���O�𗧂Ă�
	bThreadLoop = TRUE;

	// �Đ��X���b�h�N��
	DWORD dwThread;
	hThread = CreateThread(NULL, 0, PlayThread, NULL, 0, &dwThread);
	if (!hThread) {
		// ���s
		return FALSE;
	}

	// �Đ��J�n
	pAudioClient->Start();

	DEBUG("WASAPI����������\n");
	return TRUE;
}


//////////////////////////////////////////////////////////////////////////////////////
// WASAPI�̏I��
//////////////////////////////////////////////////////////////////////////////////////
void ExitWasapi()
{
	// �X���b�h���[�v�t���O�𗎂Ƃ�
	bThreadLoop = FALSE;

	// �X���b�h�I������
	if (hThread) {
		// �X���b�h�����S�ɏI������܂őҋ@
		WaitForSingleObject(hThread, INFINITE);
		CloseHandle(hThread);
		hThread = NULL;
	}

	// �C�x���g���J������
	if (hEvent) {
		CloseHandle(hEvent);
		hEvent = NULL;
	}

	// �C���^�[�t�F�[�X�J��
	SAFE_RELEASE(pRenderClient);
	SAFE_RELEASE(pAudioClient);
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pDeviceEnumerator);

	DEBUG("WASAPI�I��\n");
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