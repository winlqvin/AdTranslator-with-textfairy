/*
 * Copyright 2011, Google Inc.
 * Copyright 2011, Robert Theis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <malloc.h>
#include "android/bitmap.h"
#include "common.h"
#include "baseapi.h"
#include "ocrclass.h"
#include "allheaders.h"
#include <sstream>
#include <fstream>
#include <iostream>



static jfieldID field_mNativeData;
static jmethodID method_onProgressValues;

struct native_data_t {
  tesseract::TessBaseAPI api;
  PIX *pix;
  void *data;
  bool debug;

  Box* currentTextBox = NULL;
  l_int32 lastProgress;
  bool cancel_ocr;

  JNIEnv *cachedEnv;
  jobject* cachedObject;

  bool isStateValid() {

  	if (cancel_ocr == false && cachedEnv != NULL && cachedObject != NULL) {
  		return true;
  	} else {
  		LOGI("state is cancelled");
  		return false;

  	}
  }

  void setTextBoundaries(l_uint32 x, l_uint32 y, l_uint32 w, l_uint32 h){
	  boxSetGeometry(currentTextBox,x,y,w,h);
  }


  void initStateVariables(JNIEnv* env, jobject *object) {
  	cancel_ocr = false;
  	cachedEnv = env;
  	cachedObject = object;
  	lastProgress = 0;

  	//boxSetGeometry(currentTextBox,0,0,0,0);
  }

  void resetStateVariables() {
  	cancel_ocr = false;
  	cachedEnv = NULL;
  	cachedObject = NULL;
  	lastProgress = 0;
  	boxSetGeometry(currentTextBox,0,0,0,0);
  }


  native_data_t() {
	currentTextBox = boxCreate(0,0,0,0);
	lastProgress = 0;
    pix = NULL;
    data = NULL;
    debug = false;
    cachedEnv = NULL;
    cachedObject = NULL;
    cancel_ocr = false;
  }
  ~native_data_t(){
	 boxDestroy(&currentTextBox);
  }
};

/**
 * callback for tesseracts monitor
 */
bool cancelFunc(void* cancel_this, int words) {
	native_data_t *nat = (native_data_t*)cancel_this;
	return nat->cancel_ocr;
}

/**
 * callback for tesseracts monitor
 */
bool progressJavaCallback(void* progress_this,int progress, int left, int right, int top, int bottom) {

	native_data_t *nat = (native_data_t*)progress_this;
	if (nat->isStateValid() && nat->currentTextBox != NULL) {
		if (progress > nat->lastProgress || left != 0 || right != 0 || top != 0 || bottom != 0) {
	    	LOGI("state changed");
			int x, y, w, h;
			boxGetGeometry(nat->currentTextBox, &x, &y, &w, &h);
			nat->cachedEnv->CallVoidMethod(*(nat->cachedObject), method_onProgressValues, progress, (jint) left, (jint) right, (jint) top, (jint) bottom, (jint) x, (jint) (x + w), (jint) y, (jint) (y + h));
			nat->lastProgress = progress;
		}
	}
	return true;
}


static inline native_data_t * get_native_data(JNIEnv *env, jobject object) {
  return (native_data_t *) (env->GetLongField(object, field_mNativeData));
}

#ifdef __cplusplus
extern "C" {
#endif

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv *env;

  if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK) {
    LOGE("Failed to get the environment using GetEnv()");
    return -1;
  }

  return JNI_VERSION_1_6;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeClassInit(JNIEnv* env, 
                                                                       jclass clazz) {

  field_mNativeData = env->GetFieldID(clazz, "mNativeData", "J");
  method_onProgressValues = env->GetMethodID(clazz, "onProgressValues", "(IIIIIIIII)V");

}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeConstruct(JNIEnv* env,
                                                                       jobject object) {

  native_data_t *nat = new native_data_t;

  if (nat == NULL) {
    LOGE("%s: out of memory!", __FUNCTION__);
    return;
  }

  env->SetLongField(object, field_mNativeData, (jlong) nat);
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeFinalize(JNIEnv* env,
                                                                      jobject object) {

  native_data_t *nat = get_native_data(env, object);

  // Since Tesseract doesn't take ownership of the memory, we keep a pointer in the native
  // code struct. We need to free that pointer when we release our instance of Tesseract or
  // attempt to set a new image using one of the nativeSet* methods.
  if (nat->data != NULL)
    free(nat->data);
  else if (nat->pix != NULL)
    pixDestroy(&nat->pix);
  nat->data = NULL;
  nat->pix = NULL;

  if (nat != NULL)
    delete nat;
}

jboolean Java_com_googlecode_tesseract_android_TessBaseAPI_nativeInit(JNIEnv *env,
                                                                      jobject thiz,
                                                                      jstring dir,
                                                                      jstring lang) {

  native_data_t *nat = get_native_data(env, thiz);

  const char *c_dir = env->GetStringUTFChars(dir, NULL);
  const char *c_lang = env->GetStringUTFChars(lang, NULL);

  jboolean res = JNI_TRUE;

  if (nat->api.Init(c_dir, c_lang)) {
    LOGE("Could not initialize Tesseract API with language=%s!", c_lang);
    res = JNI_FALSE;
  } else {
    LOGI("Initialized Tesseract API with language=%s", c_lang);
  }

  env->ReleaseStringUTFChars(dir, c_dir);
  env->ReleaseStringUTFChars(lang, c_lang);

  return res;
}

jboolean Java_com_googlecode_tesseract_android_TessBaseAPI_nativeInitOem(JNIEnv *env, 
                                                                         jobject thiz,
                                                                         jstring dir, 
                                                                         jstring lang, 
                                                                         jint mode) {

  native_data_t *nat = get_native_data(env, thiz);

  const char *c_dir = env->GetStringUTFChars(dir, NULL);
  const char *c_lang = env->GetStringUTFChars(lang, NULL);

  jboolean res = JNI_TRUE;

  if (nat->api.Init(c_dir, c_lang, (tesseract::OcrEngineMode) mode)) {
    LOGE("Could not initialize Tesseract API with language=%s!", c_lang);
    res = JNI_FALSE;
  } else {
    LOGI("Initialized Tesseract API with language=%s", c_lang);
  }

  env->ReleaseStringUTFChars(dir, c_dir);
  env->ReleaseStringUTFChars(lang, c_lang);

  return res;
}

jstring Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetInitLanguagesAsString(JNIEnv *env,
                                                                                         jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  const char *text = nat->api.GetInitLanguagesAsString();

  jstring result = env->NewStringUTF(text);

  return result;
}


void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetImageBytes(JNIEnv *env,
                                                                           jobject thiz,
                                                                           jbyteArray data,
                                                                           jint width,
                                                                           jint height,
                                                                           jint bpp,
                                                                           jint bpl) {

  jbyte *data_array = env->GetByteArrayElements(data, NULL);
  int count = env->GetArrayLength(data);
  unsigned char* imagedata = (unsigned char *) malloc(count * sizeof(unsigned char));

  // This is painfully slow, but necessary because we don't know
  // how many bits the JVM might be using to represent a byte
  for (int i = 0; i < count; i++) {
    imagedata[i] = (unsigned char) data_array[i];
  }

  env->ReleaseByteArrayElements(data, data_array, JNI_ABORT);

  native_data_t *nat = get_native_data(env, thiz);
  nat->api.SetImage(imagedata, (int) width, (int) height, (int) bpp, (int) bpl);

  // Since Tesseract doesn't take ownership of the memory, we keep a pointer in the native
  // code struct. We need to free that pointer when we release our instance of Tesseract or
  // attempt to set a new image using one of the nativeSet* methods.
  if (nat->data != NULL)
    free(nat->data);
  else if (nat->pix != NULL)
    pixDestroy(&nat->pix);
  nat->data = imagedata;
  nat->pix = NULL;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetImagePix(JNIEnv *env,
                                                                         jobject thiz,
                                                                         jlong nativePix) {

  PIX *pixs = (PIX *) nativePix;
  PIX *pixd = pixClone(pixs);

  native_data_t *nat = get_native_data(env, thiz);
  if(pixd){
	l_int32 width = pixGetWidth(pixd);
	l_int32 height = pixGetHeight(pixd);
	nat->setTextBoundaries(0,0,width,height);
  	LOGI("setting ocr box %i,%i",width,height);
  }
  nat->api.SetImage(pixd);
  // Since Tesseract doesn't take ownership of the memory, we keep a pointer in the native
  // code struct. We need to free that pointer when we release our instance of Tesseract or
  // attempt to set a new image using one of the nativeSet* methods.
  if (nat->data != NULL)
    free(nat->data);
  else if (nat->pix != NULL)
    pixDestroy(&nat->pix);
  nat->data = NULL;
  nat->pix = pixd;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetRectangle(JNIEnv *env,
                                                                          jobject thiz,
                                                                          jint left,
                                                                          jint top,
                                                                          jint width,
                                                                          jint height) {

  native_data_t *nat = get_native_data(env, thiz);
  nat->setTextBoundaries(left,top,width, height);

  nat->api.SetRectangle(left, top, width, height);
}

jstring Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetUTF8Text(JNIEnv *env,
                                                                            jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  char *text = nat->api.GetUTF8Text();

  jstring result = env->NewStringUTF(text);

  free(text);

  return result;
}




std::string GetHTMLText(tesseract::ResultIterator* res_it, const float minConfidenceToShowColor) {
	int lcnt = 1, bcnt = 1, pcnt = 1, wcnt = 1;
	std::ostringstream html_str;
	bool isItalic = false;
	bool para_open = false;

	for (; !res_it->Empty(tesseract::RIL_BLOCK); wcnt++) {
		if (res_it->Empty(tesseract::RIL_WORD)) {
			res_it->Next(tesseract::RIL_WORD);
			continue;
		}

		if (res_it->IsAtBeginningOf(tesseract::RIL_PARA)) {
			if (para_open) {
				html_str << "</p>";
				pcnt++;
			}
			html_str << "<p>";
			para_open = true;
		}

		// Now, process the word...
		const char *font_name;
		bool bold, italic, underlined, monospace, serif, smallcaps;
		int pointsize, font_id;
		font_name = res_it->WordFontAttributes(&bold, &italic, &underlined,
				&monospace, &serif, &smallcaps, &pointsize, &font_id);

		float confidence = res_it->Confidence(tesseract::RIL_WORD);
		bool addConfidence = false;

		if (italic && !isItalic) {
			html_str << "<strong>";
			isItalic = true;
		} else if (!italic && isItalic) {
			html_str << "</strong>";
			isItalic = false;
		}

		char* word = res_it->GetUTF8Text(tesseract::RIL_WORD);
		bool isSpace = strcmp(word, " ") == 0;
		delete[] word;
		if (confidence < minConfidenceToShowColor && !isSpace) {
			addConfidence = true;
			html_str << "<font conf='";
			html_str << (int) confidence;
			html_str << "' color='#DE2222'>";
		}

		do {
			const char *grapheme = res_it->GetUTF8Text(tesseract::RIL_SYMBOL);
			if (grapheme && grapheme[0] != 0) {
				if (grapheme[1] == 0) {
					switch (grapheme[0]) {
					case '<':
						html_str << "&lt;";
						break;
					case '>':
						html_str << "&gt;";
						break;
					case '&':
						html_str << "&amp;";
						break;
					case '"':
						html_str << "&quot;";
						break;
					case '\'':
						html_str << "&#39;";
						break;
					default:
						html_str << grapheme;
						break;
					}
				} else {
					html_str << grapheme;
				}
			}
			delete[] grapheme;
			res_it->Next(tesseract::RIL_SYMBOL);
		} while (!res_it->Empty(tesseract::RIL_BLOCK)
				&& !res_it->IsAtBeginningOf(tesseract::RIL_WORD));

		if (addConfidence == true) {
			html_str << "</font>";
		}

		html_str << " ";
	}
	if (isItalic) {
		html_str << "</strong>";
	}
	if (para_open) {
		html_str << "</p>";
		pcnt++;
	}
	return html_str.str();
}

jstring Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetHtmlText(JNIEnv *env,
                                                                            jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  tesseract::ResultIterator* res_it = nat->api.GetIterator();
  std::string utf8text = GetHTMLText(res_it, 70);
  jstring result = env->NewStringUTF(utf8text.c_str());
  return result;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeStop(JNIEnv *env, 
                                                                  jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  // stop by setting a flag thats used by the monitor
	nat->resetStateVariables();
	nat->cancel_ocr = true;
}

jint Java_com_googlecode_tesseract_android_TessBaseAPI_nativeMeanConfidence(JNIEnv *env,
                                                                            jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  return (jint) nat->api.MeanTextConf();
}

jintArray Java_com_googlecode_tesseract_android_TessBaseAPI_nativeWordConfidences(JNIEnv *env,
                                                                                  jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  int *confs = nat->api.AllWordConfidences();

  if (confs == NULL) {
    LOGE("Could not get word-confidence values!");
    return NULL;
  }

  int len, *trav;
  for (len = 0, trav = confs; *trav != -1; trav++, len++)
    ;

  LOG_ASSERT((confs != NULL), "Confidence array has %d elements", len);

  jintArray ret = env->NewIntArray(len);

  LOG_ASSERT((ret != NULL), "Could not create Java confidence array!");

  env->SetIntArrayRegion(ret, 0, len, confs);

  delete[] confs;

  return ret;
}

jboolean Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetVariable(JNIEnv *env,
                                                                             jobject thiz,
                                                                             jstring var,
                                                                             jstring value) {

  native_data_t *nat = get_native_data(env, thiz);

  const char *c_var = env->GetStringUTFChars(var, NULL);
  const char *c_value = env->GetStringUTFChars(value, NULL);

  jboolean set = nat->api.SetVariable(c_var, c_value) ? JNI_TRUE : JNI_FALSE;

  env->ReleaseStringUTFChars(var, c_var);
  env->ReleaseStringUTFChars(value, c_value);

  return set;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeClear(JNIEnv *env,
                                                                   jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  nat->api.Clear();

  // Call between pages or documents etc to free up memory and forget adaptive data.
  nat->api.ClearAdaptiveClassifier();

  // Since Tesseract doesn't take ownership of the memory, we keep a pointer in the native
  // code struct. We need to free that pointer when we release our instance of Tesseract or
  // attempt to set a new image using one of the nativeSet* methods.
  if (nat->data != NULL)
    free(nat->data);
  else if (nat->pix != NULL)
    pixDestroy(&nat->pix);
  nat->data = NULL;
  nat->pix = NULL;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeEnd(JNIEnv *env,
                                                                 jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  nat->api.End();

  // Since Tesseract doesn't take ownership of the memory, we keep a pointer in the native
  // code struct. We need to free that pointer when we release our instance of Tesseract or
  // attempt to set a new image using one of the nativeSet* methods.
  if (nat->data != NULL)
    free(nat->data);
  else if (nat->pix != NULL)
    pixDestroy(&nat->pix);
  nat->data = NULL;
  nat->pix = NULL;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetDebug(JNIEnv *env,
                                                                      jobject thiz,
                                                                      jboolean debug) {

  native_data_t *nat = get_native_data(env, thiz);

  nat->debug = (debug == JNI_TRUE) ? TRUE : FALSE;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetPageSegMode(JNIEnv *env,
                                                                            jobject thiz,
                                                                            jint mode) {

  native_data_t *nat = get_native_data(env, thiz);

  nat->api.SetPageSegMode((tesseract::PageSegMode) mode);
}

jlong Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetThresholdedImage(JNIEnv *env,
                                                                                  jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);

  PIX *pix = nat->api.GetThresholdedImage();

  return (jlong) pix;
}

jlong Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetRegions(JNIEnv *env,
                                                                         jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);;
  PIXA *pixa = NULL;
  BOXA *boxa;

  boxa = nat->api.GetRegions(&pixa);

  boxaDestroy(&boxa);

  return reinterpret_cast<jlong>(pixa);
}

jlong Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetTextlines(JNIEnv *env,
                                                                           jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);;
  PIXA *pixa = NULL;
  BOXA *boxa;

  boxa = nat->api.GetTextlines(&pixa, NULL);

  boxaDestroy(&boxa);

  return reinterpret_cast<jlong>(pixa);
}

jlong Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetStrips(JNIEnv *env,
                                                                        jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);;
  PIXA *pixa = NULL;
  BOXA *boxa;

  boxa = nat->api.GetStrips(&pixa, NULL);

  boxaDestroy(&boxa);

  return reinterpret_cast<jlong>(pixa);
}

jlong Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetWords(JNIEnv *env,
                                                                       jobject thiz) {

  native_data_t *nat = get_native_data(env, thiz);;
  PIXA *pixa = NULL;
  BOXA *boxa;

  boxa = nat->api.GetWords(&pixa);

  boxaDestroy(&boxa);

  return reinterpret_cast<jlong>(pixa);
}

jlong Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetResultIterator(JNIEnv *env,
                                                                                jobject thiz) {
  native_data_t *nat = get_native_data(env, thiz);

  return (jlong) nat->api.GetIterator();
}


jstring Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetHOCRText(JNIEnv *env,
                                                                            jobject thiz, jint page) {



  native_data_t *nat = get_native_data(env, thiz);
  nat->initStateVariables(env, &thiz);

  ETEXT_DESC monitor;
  monitor.progress_callback = progressJavaCallback;
  monitor.cancel = cancelFunc;
  monitor.cancel_this = nat;
  monitor.progress_this = nat;

  char *text = nat->api.GetHOCRText(page,&monitor);

  jstring result = env->NewStringUTF(text);

  free(text);
  nat->resetStateVariables();

  return result;
}

jstring Java_com_googlecode_tesseract_android_TessBaseAPI_nativeGetBoxText(JNIEnv *env,
                                                                           jobject thiz, jint page) {

  native_data_t *nat = get_native_data(env, thiz);

  char *text = nat->api.GetBoxText(page);

  jstring result = env->NewStringUTF(text);

  free(text);

  return result;
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetInputName(JNIEnv *env,
                                                                          jobject thiz,
                                                                          jstring name) {
  native_data_t *nat = get_native_data(env, thiz);
  const char *c_name = env->GetStringUTFChars(name, NULL);
  nat->api.SetInputName(c_name);
  env->ReleaseStringUTFChars(name, c_name);
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeSetOutputName(JNIEnv *env,
                                                                           jobject thiz,
                                                                           jstring name) {
  native_data_t *nat = get_native_data(env, thiz);
  const char *c_name = env->GetStringUTFChars(name, NULL);
  nat->api.SetOutputName(c_name);
  env->ReleaseStringUTFChars(name, c_name);
}

void Java_com_googlecode_tesseract_android_TessBaseAPI_nativeReadConfigFile(JNIEnv *env,
                                                                            jobject thiz,
                                                                            jstring fileName) {
  native_data_t *nat = get_native_data(env, thiz);
  const char *c_file_name = env->GetStringUTFChars(fileName, NULL);
  nat->api.ReadConfigFile(c_file_name);
  env->ReleaseStringUTFChars(fileName, c_file_name);
}
#ifdef __cplusplus
}
#endif
