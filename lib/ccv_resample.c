#include "ccv.h"
#include "ccv_internal.h"

/* area interpolation resample is adopted from OpenCV */

typedef struct {
	int si, di;
	unsigned int alpha;
} ccv_int_alpha;

static void _ccv_resample_area_8u(ccv_dense_matrix_t* a, ccv_dense_matrix_t* b)
{
	ccv_int_alpha* xofs = (ccv_int_alpha*)alloca(sizeof(ccv_int_alpha) * a->cols * 2);
	int ch = ccv_clamp(CCV_GET_CHANNEL(a->type), 1, 4);
	double scale_x = (double)a->cols / b->cols;
	double scale_y = (double)a->rows / b->rows;
	// double scale = 1.f / (scale_x * scale_y);
	unsigned int inv_scale_256 = (int)(scale_x * scale_y * 0x10000);
	int dx, dy, sx, sy, i, k;
	for (dx = 0, k = 0; dx < b->cols; dx++)
	{
		double fsx1 = dx * scale_x, fsx2 = fsx1 + scale_x;
		int sx1 = (int)(fsx1 + 1.0 - 1e-6), sx2 = (int)(fsx2);
		sx1 = ccv_min(sx1, a->cols - 1);
		sx2 = ccv_min(sx2, a->cols - 1);

		if (sx1 > fsx1)
		{
			xofs[k].di = dx * ch;
			xofs[k].si = (sx1 - 1) * ch;
			xofs[k++].alpha = (unsigned int)((sx1 - fsx1) * 0x100);
		}

		for (sx = sx1; sx < sx2; sx++)
		{
			xofs[k].di = dx * ch;
			xofs[k].si = sx * ch;
			xofs[k++].alpha = 256;
		}

		if (fsx2 - sx2 > 1e-3)
		{
			xofs[k].di = dx * ch;
			xofs[k].si = sx2 * ch;
			xofs[k++].alpha = (unsigned int)((fsx2 - sx2) * 256);
		}
	}
	int xofs_count = k;
	unsigned int* buf = (unsigned int*)alloca(b->cols * ch * sizeof(unsigned int));
	unsigned int* sum = (unsigned int*)alloca(b->cols * ch * sizeof(unsigned int));
	for (dx = 0; dx < b->cols * ch; dx++)
		buf[dx] = sum[dx] = 0;
	dy = 0;
	for (sy = 0; sy < a->rows; sy++)
	{
		unsigned char* a_ptr = a->data.u8 + a->step * sy;
		for (k = 0; k < xofs_count; k++)
		{
			int dxn = xofs[k].di;
			unsigned int alpha = xofs[k].alpha;
			for (i = 0; i < ch; i++)
				buf[dxn + i] += a_ptr[xofs[k].si + i] * alpha;
		}
		if ((dy + 1) * scale_y <= sy + 1 || sy == a->rows - 1)
		{
			unsigned int beta = (int)(ccv_max(sy + 1 - (dy + 1) * scale_y, 0.f) * 256);
			unsigned int beta1 = 256 - beta;
			unsigned char* b_ptr = b->data.u8 + b->step * dy;
			if (beta <= 0)
			{
				for (dx = 0; dx < b->cols * ch; dx++)
				{
					b_ptr[dx] = ccv_clamp((sum[dx] + buf[dx] * 256) / inv_scale_256, 0, 255);
					sum[dx] = buf[dx] = 0;
				}
			} else {
				for (dx = 0; dx < b->cols * ch; dx++)
				{
					b_ptr[dx] = ccv_clamp((sum[dx] + buf[dx] * beta1) / inv_scale_256, 0, 255);
					sum[dx] = buf[dx] * beta;
					buf[dx] = 0;
				}
			}
			dy++;
		}
		else
		{
			for(dx = 0; dx < b->cols * ch; dx++)
			{
				sum[dx] += buf[dx] * 256;
				buf[dx] = 0;
			}
		}
	}
}

typedef struct {
	int si, di;
	float alpha;
} ccv_decimal_alpha;

static void _ccv_resample_area(ccv_dense_matrix_t* a, ccv_dense_matrix_t* b)
{
	ccv_decimal_alpha* xofs = (ccv_decimal_alpha*)alloca(sizeof(ccv_decimal_alpha) * a->cols * 2);
	int ch = CCV_GET_CHANNEL(a->type);
	double scale_x = (double)a->cols / b->cols;
	double scale_y = (double)a->rows / b->rows;
	double scale = 1.f / (scale_x * scale_y);
	int dx, dy, sx, sy, i, k;
	for (dx = 0, k = 0; dx < b->cols; dx++)
	{
		double fsx1 = dx * scale_x, fsx2 = fsx1 + scale_x;
		int sx1 = (int)(fsx1 + 1.0 - 1e-6), sx2 = (int)(fsx2);
		sx1 = ccv_min(sx1, a->cols - 1);
		sx2 = ccv_min(sx2, a->cols - 1);

		if (sx1 > fsx1)
		{
			xofs[k].di = dx * ch;
			xofs[k].si = (sx1 - 1) * ch;
			xofs[k++].alpha = (float)((sx1 - fsx1) * scale);
		}

		for (sx = sx1; sx < sx2; sx++)
		{
			xofs[k].di = dx * ch;
			xofs[k].si = sx * ch;
			xofs[k++].alpha = (float)scale;
		}

		if (fsx2 - sx2 > 1e-3)
		{
			xofs[k].di = dx * ch;
			xofs[k].si = sx2 * ch;
			xofs[k++].alpha = (float)((fsx2 - sx2) * scale);
		}
	}
	int xofs_count = k;
	float* buf = (float*)alloca(b->cols * ch * sizeof(float));
	float* sum = (float*)alloca(b->cols * ch * sizeof(float));
	for (dx = 0; dx < b->cols * ch; dx++)
		buf[dx] = sum[dx] = 0;
	dy = 0;
#define for_block(_for_get, _for_set) \
	for (sy = 0; sy < a->rows; sy++) \
	{ \
		unsigned char* a_ptr = a->data.u8 + a->step * sy; \
		for (k = 0; k < xofs_count; k++) \
		{ \
			int dxn = xofs[k].di; \
			float alpha = xofs[k].alpha; \
			for (i = 0; i < ch; i++) \
				buf[dxn + i] += _for_get(a_ptr, xofs[k].si + i, 0) * alpha; \
		} \
		if ((dy + 1) * scale_y <= sy + 1 || sy == a->rows - 1) \
		{ \
			float beta = ccv_max(sy + 1 - (dy + 1) * scale_y, 0.f); \
			float beta1 = 1 - beta; \
			unsigned char* b_ptr = b->data.u8 + b->step * dy; \
			if (fabs(beta) < 1e-3) \
			{ \
				for (dx = 0; dx < b->cols * ch; dx++) \
				{ \
					_for_set(b_ptr, dx, sum[dx] + buf[dx], 0); \
					sum[dx] = buf[dx] = 0; \
				} \
			} else { \
				for (dx = 0; dx < b->cols * ch; dx++) \
				{ \
					_for_set(b_ptr, dx, sum[dx] + buf[dx] * beta1, 0); \
					sum[dx] = buf[dx] * beta; \
					buf[dx] = 0; \
				} \
			} \
			dy++; \
		} \
		else \
		{ \
			for(dx = 0; dx < b->cols * ch; dx++) \
			{ \
				sum[dx] += buf[dx]; \
				buf[dx] = 0; \
			} \
		} \
	}
	ccv_matrix_getter(a->type, ccv_matrix_setter, b->type, for_block);
#undef for_block
}

void ccv_resample(ccv_dense_matrix_t* a, ccv_dense_matrix_t** b, int btype, int rows, int cols, int type)
{
	ccv_declare_derived_signature(sig, a->sig != 0, ccv_sign_with_format(64, "ccv_resample(%d,%d,%d)", rows, cols, type), a->sig, 0);
	btype = (btype == 0) ? CCV_GET_DATA_TYPE(a->type) | CCV_GET_CHANNEL(a->type) : CCV_GET_DATA_TYPE(btype) | CCV_GET_CHANNEL(a->type);
	ccv_dense_matrix_t* db = *b = ccv_dense_matrix_renew(*b, rows, cols, CCV_ALL_DATA_TYPE | CCV_GET_CHANNEL(a->type), btype, sig);
	ccv_object_return_if_cached(, db);
	if (a->rows == db->rows && a->cols == db->cols)
	{
		if (CCV_GET_CHANNEL(a->type) == CCV_GET_CHANNEL(db->type) && CCV_GET_DATA_TYPE(db->type) == CCV_GET_DATA_TYPE(a->type))
			memcpy(db->data.u8, a->data.u8, a->rows * a->step);
		else {
			ccv_shift(a, (ccv_matrix_t**)&db, 0, 0, 0);
		}
		return;
	}
	switch (type)
	{
		case CCV_INTER_AREA:
			if (a->rows > db->rows && a->cols > db->cols)
			{
				/* using the fast alternative (fix point scale, 0x100 to avoid overflow) */
				if (CCV_GET_DATA_TYPE(a->type) == CCV_8U && CCV_GET_DATA_TYPE(db->type) == CCV_8U && a->rows * a->cols / (db->rows * db->cols) < 0x100)
					_ccv_resample_area_8u(a, db);
				else
					_ccv_resample_area(a, db);
				break;
			}
		case CCV_INTER_LINEAR:
			break;
		case CCV_INTER_CUBIC:
			break;
		case CCV_INTER_LANCZOS:
			break;
	}
}

/* the following code is adopted from OpenCV cvPyrDown */
void ccv_sample_down(ccv_dense_matrix_t* a, ccv_dense_matrix_t** b, int type, int src_x, int src_y)
{
	assert(src_x >= 0 && src_y >= 0);
	ccv_declare_derived_signature(sig, a->sig != 0, ccv_sign_with_format(64, "ccv_sample_down(%d,%d)", src_x, src_y), a->sig, 0);
	type = (type == 0) ? CCV_GET_DATA_TYPE(a->type) | CCV_GET_CHANNEL(a->type) : CCV_GET_DATA_TYPE(type) | CCV_GET_CHANNEL(a->type);
	ccv_dense_matrix_t* db = *b = ccv_dense_matrix_renew(*b, a->rows / 2, a->cols / 2, CCV_ALL_DATA_TYPE | CCV_GET_CHANNEL(a->type), type, sig);
	ccv_object_return_if_cached(, db);
	int ch = CCV_GET_CHANNEL(a->type);
	int cols0 = db->cols - 1 - src_x;
	int dy, sy = -2 + src_y, sx = src_x * ch, dx, k;
	int* tab = (int*)alloca((a->cols + src_x + 2) * ch * sizeof(int));
	for (dx = 0; dx < a->cols + src_x + 2; dx++)
		for (k = 0; k < ch; k++)
			tab[dx * ch + k] = ((dx >= a->cols) ? a->cols * 2 - 1 - dx : dx) * ch + k;
	unsigned char* buf = (unsigned char*)alloca(5 * db->cols * ch * ccv_max(CCV_GET_DATA_TYPE_SIZE(db->type), sizeof(int)));
	int bufstep = db->cols * ch * ccv_max(CCV_GET_DATA_TYPE_SIZE(db->type), sizeof(int));
	unsigned char* b_ptr = db->data.u8;
	/* why is src_y * 4 in computing the offset of row?
	 * Essentially, it means sy - src_y but in a manner that doesn't result negative number.
	 * notice that we added src_y before when computing sy in the first place, however,
	 * it is not desirable to have that offset when we try to wrap it into our 5-row buffer (
	 * because in later rearrangement, we have no src_y to backup the arrangement). In
	 * such micro scope, we managed to stripe 5 addition into one shift and addition. */
#define for_block(_for_get_a, _for_set, _for_get, _for_set_b) \
	for (dy = 0; dy < db->rows; dy++) \
	{ \
		for(; sy <= dy * 2 + 2 + src_y; sy++) \
		{ \
			unsigned char* row = buf + ((sy + src_y * 4 + 2) % 5) * bufstep; \
			int _sy = (sy < 0) ? -1 - sy : (sy >= a->rows) ? a->rows * 2 - 1 - sy : sy; \
			unsigned char* a_ptr = a->data.u8 + a->step * _sy; \
			for (k = 0; k < ch; k++) \
				_for_set(row, k, _for_get_a(a_ptr, sx + k, 0) * 10 + _for_get_a(a_ptr, ch + sx + k, 0) * 5 + _for_get_a(a_ptr, 2 * ch + sx + k, 0), 0); \
			for(dx = ch; dx < cols0 * ch; dx += ch) \
				for (k = 0; k < ch; k++) \
					_for_set(row, dx + k, _for_get_a(a_ptr, dx * 2 + sx + k, 0) * 6 + (_for_get_a(a_ptr, dx * 2 + sx + k - ch, 0) + _for_get_a(a_ptr, dx * 2 + sx + k + ch, 0)) * 4 + _for_get_a(a_ptr, dx * 2 + sx + k - ch * 2, 0) + _for_get_a(a_ptr, dx * 2 + sx + k + ch * 2, 0), 0); \
			x_block(_for_get_a, _for_set, _for_get, _for_set_b); \
		} \
		unsigned char* rows[5]; \
		for(k = 0; k < 5; k++) \
			rows[k] = buf + ((dy * 2 + k) % 5) * bufstep; \
		for(dx = 0; dx < db->cols * ch; dx++) \
			_for_set_b(b_ptr, dx, (_for_get(rows[2], dx, 0) * 6 + (_for_get(rows[1], dx, 0) + _for_get(rows[3], dx, 0)) * 4 + _for_get(rows[0], dx, 0) + _for_get(rows[4], dx, 0)) / 256, 0); \
		b_ptr += db->step; \
	}
	int no_8u_type = (a->type & CCV_8U) ? CCV_32S : a->type;
	if (src_x > 0)
	{
#define x_block(_for_get_a, _for_set, _for_get, _for_set_b) \
		for (dx = cols0 * ch; dx < db->cols * ch; dx += ch) \
			for (k = 0; k < ch; k++) \
				_for_set(row, dx + k, _for_get_a(a_ptr, tab[dx * 2 + sx + k], 0) * 6 + (_for_get_a(a_ptr, tab[dx * 2 + sx + k - ch], 0) + _for_get_a(a_ptr, tab[dx * 2 + sx + k + ch], 0)) * 4 + _for_get_a(a_ptr, tab[dx * 2 + sx + k - ch * 2], 0) + _for_get_a(a_ptr, tab[dx * 2 + sx + k + ch * 2], 0), 0);
		ccv_matrix_getter_a(a->type, ccv_matrix_setter_getter, no_8u_type, ccv_matrix_setter_b, db->type, for_block);
#undef x_block
	} else {
#define x_block(_for_get_a, _for_set, _for_get, _for_set_b) \
		for (k = 0; k < ch; k++) \
			_for_set(row, (db->cols - 1) * ch + k, _for_get_a(a_ptr, a->cols * ch + sx - ch + k, 0) * 10 + _for_get_a(a_ptr, (a->cols - 2) * ch + sx + k, 0) * 5 + _for_get_a(a_ptr, (a->cols - 3) * ch + sx + k, 0), 0);
		ccv_matrix_getter_a(a->type, ccv_matrix_setter_getter, no_8u_type, ccv_matrix_setter_b, db->type, for_block);
#undef x_block
	}
#undef for_block
}

void ccv_sample_up(ccv_dense_matrix_t* a, ccv_dense_matrix_t** b, int type, int src_x, int src_y)
{
	assert(src_x >= 0 && src_y >= 0);
	ccv_declare_derived_signature(sig, a->sig != 0, ccv_sign_with_format(64, "ccv_sample_up(%d,%d)", src_x, src_y), a->sig, 0);
	type = (type == 0) ? CCV_GET_DATA_TYPE(a->type) | CCV_GET_CHANNEL(a->type) : CCV_GET_DATA_TYPE(type) | CCV_GET_CHANNEL(a->type);
	ccv_dense_matrix_t* db = *b = ccv_dense_matrix_renew(*b, a->rows * 2, a->cols * 2, CCV_ALL_DATA_TYPE | CCV_GET_CHANNEL(a->type), type, sig);
	ccv_object_return_if_cached(, db);
	int ch = CCV_GET_CHANNEL(a->type);
	int cols0 = a->cols - 1 - src_x;
	int y, x, sy = -1 + src_y, sx = src_x * ch, k;
	int* tab = (int*)alloca((a->cols + src_x + 2) * ch * sizeof(int));
	for (x = 0; x < a->cols + src_x + 2; x++)
		for (k = 0; k < ch; k++)
			tab[x * ch + k] = ((x >= a->cols) ? a->cols * 2 - 1 - x : x) * ch + k;
	unsigned char* buf = (unsigned char*)alloca(3 * db->cols * ch * ccv_max(CCV_GET_DATA_TYPE_SIZE(db->type), sizeof(int)));
	int bufstep = db->cols * ch * ccv_max(CCV_GET_DATA_TYPE_SIZE(db->type), sizeof(int));
	unsigned char* b_ptr = db->data.u8;
	/* why src_y * 2: the same argument as in ccv_sample_down */
#define for_block(_for_get_a, _for_set, _for_get, _for_set_b) \
	for (y = 0; y < a->rows; y++) \
	{ \
		for (; sy <= y + 1 + src_y; sy++) \
		{ \
			unsigned char* row = buf + ((sy + src_y * 2 + 1) % 3) * bufstep; \
			int _sy = (sy < 0) ? -1 - sy : (sy >= a->rows) ? a->rows * 2 - 1 - sy : sy; \
			unsigned char* a_ptr = a->data.u8 + a->step * _sy; \
			if (a->cols == 1) \
			{ \
				for (k = 0; k < ch; k++) \
				{ \
					_for_set(row, k, _for_get_a(a_ptr, k, 0) * (G025 + G075 + G125), 0); \
					_for_set(row, k + ch, _for_get_a(a_ptr, k, 0) * (G025 + G075 + G125), 0); \
				} \
				continue; \
			} \
			if (sx == 0) \
			{ \
				for (k = 0; k < ch; k++) \
				{ \
					_for_set(row, k, _for_get_a(a_ptr, k + sx, 0) * (G025 + G075) + _for_get_a(a_ptr, k + sx + ch, 0) * G125, 0); \
					_for_set(row, k + ch, _for_get_a(a_ptr, k + sx, 0) * (G125 + G025) + _for_get_a(a_ptr, k + sx + ch, 0) * G075, 0); \
				} \
			} \
			/* some serious flaw in computing Gaussian weighting in previous version
			 * specially, we are doing perfect upsampling (2x) so, it concerns a grid like:
			 * XXYY
			 * XXYY
			 * in this case, to upsampling, the weight should be from distance 0.25 and 1.25, and 0.25 and 0.75
			 * previously, it was mistakingly be 0.0 1.0, 0.5 0.5 (imperfect upsampling (2x - 1)) */ \
			for (x = (sx == 0) ? ch : 0; x < cols0 * ch; x += ch) \
			{ \
				for (k = 0; k < ch; k++) \
				{ \
					_for_set(row, x * 2 + k, _for_get_a(a_ptr, x + sx - ch + k, 0) * G075 + _for_get_a(a_ptr, x + sx + k, 0) * G025 + _for_get_a(a_ptr, x + sx + ch + k, 0) * G125, 0); \
					_for_set(row, x * 2 + ch + k, _for_get_a(a_ptr, x + sx - ch + k, 0) * G125 + _for_get_a(a_ptr, x + sx + k, 0) * G025 + _for_get_a(a_ptr, x + sx + ch + k, 0) * G075, 0); \
				} \
			} \
			x_block(_for_get_a, _for_set, _for_get, _for_set_b); \
		} \
		unsigned char* rows[3]; \
		for (k = 0; k < 3; k++) \
			rows[k] = buf + ((y + k) % 3) * bufstep; \
		for (x = 0; x < db->cols * ch; x++) \
		{ \
			_for_set_b(b_ptr, x, (_for_get(rows[0], x, 0) * G075 + _for_get(rows[1], x, 0) * G025 + _for_get(rows[2], x, 0) * G125) / GALL, 0); \
			_for_set_b(b_ptr + db->step, x, (_for_get(rows[0], x, 0) * G125 + _for_get(rows[1], x, 0) * G025 + _for_get(rows[2], x, 0) * G075) / GALL, 0); \
		} \
		b_ptr += 2 * db->step; \
	}
	int no_8u_type = (a->type & CCV_8U) ? CCV_32S : a->type;
	/* unswitch if condition in manual way */
	if ((a->type & CCV_8U) || (a->type & CCV_32S) || (a->type & CCV_64S))
	{
#define G025 (23)
#define G075 (8)
#define G125 (1)
#define GALL (1024)
		if (src_x > 0)
		{
#define x_block(_for_get_a, _for_set, _for_get, _for_set_b) \
			for (x = cols0 * ch; x < a->cols * ch; x += ch) \
				for (k = 0; k < ch; k++) \
				{ \
					_for_set(row, x * 2 + k, _for_get_a(a_ptr, tab[x + sx - ch + k], 0) * G075 + _for_get_a(a_ptr, tab[x + sx + k], 0) * G025 + _for_get_a(a_ptr, tab[x + sx + ch + k], 0) * G125, 0); \
					_for_set(row, x * 2 + ch + k, _for_get_a(a_ptr, tab[x + sx - ch + k], 0) * G125 + _for_get_a(a_ptr, tab[x + sx + k], 0) * G025 + _for_get_a(a_ptr, tab[x + sx + ch + k], 0) * G075, 0); \
			}
			ccv_matrix_getter_integer_only(a->type, ccv_matrix_setter_getter_integer_only, no_8u_type, ccv_matrix_setter_b, db->type, for_block);
#undef x_block
		} else {
#define x_block(_for_get_a, _for_set, _for_get, _for_set_b) \
			for (k = 0; k < ch; k++) \
			{ \
				_for_set(row, (a->cols - 1) * 2 * ch + k, _for_get_a(a_ptr, (a->cols - 2) * ch + k, 0) * G075 + _for_get_a(a_ptr, (a->cols - 1) * ch + k, 0) * (G025 + G125), 0); \
				_for_set(row, (a->cols - 1) * 2 * ch + ch + k, _for_get_a(a_ptr, (a->cols - 2) * ch + k, 0) * G125 + _for_get_a(a_ptr, (a->cols - 1) * ch + k, 0) * (G025 + G075), 0); \
			}
			ccv_matrix_getter_integer_only(a->type, ccv_matrix_setter_getter_integer_only, no_8u_type, ccv_matrix_setter_b, db->type, for_block);
#undef x_block
		}
#undef GALL
#undef G125
#undef G075
#undef G025
	} else {
#define G025 (0.705385)
#define G075 (0.259496)
#define G125 (0.035119)
#define GALL (1)
		if (src_x > 0)
		{
#define x_block(_for_get_a, _for_set, _for_get, _for_set_b) \
			for (x = cols0 * ch; x < a->cols * ch; x += ch) \
				for (k = 0; k < ch; k++) \
				{ \
					_for_set(row, x * 2 + k, _for_get_a(a_ptr, tab[x + sx - ch + k], 0) * G075 + _for_get_a(a_ptr, tab[x + sx + k], 0) * G025 + _for_get_a(a_ptr, tab[x + sx + ch + k], 0) * G125, 0); \
					_for_set(row, x * 2 + ch + k, _for_get_a(a_ptr, tab[x + sx - ch + k], 0) * G125 + _for_get_a(a_ptr, tab[x + sx + k], 0) * G025 + _for_get_a(a_ptr, tab[x + sx + ch + k], 0) * G075, 0); \
			}
			ccv_matrix_getter_float_only(a->type, ccv_matrix_setter_getter_float_only, no_8u_type, ccv_matrix_setter_b, db->type, for_block);
#undef x_block
		} else {
#define x_block(_for_get_a, _for_set, _for_get, _for_set_b) \
			for (k = 0; k < ch; k++) \
			{ \
				_for_set(row, (a->cols - 1) * 2 * ch + k, _for_get_a(a_ptr, (a->cols - 2) * ch + k, 0) * G075 + _for_get_a(a_ptr, (a->cols - 1) * ch + k, 0) * (G025 + G125), 0); \
				_for_set(row, (a->cols - 1) * 2 * ch + ch + k, _for_get_a(a_ptr, (a->cols - 2) * ch + k, 0) * G125 + _for_get_a(a_ptr, (a->cols - 1) * ch + k, 0) * (G025 + G075), 0); \
			}
			ccv_matrix_getter_float_only(a->type, ccv_matrix_setter_getter_float_only, no_8u_type, ccv_matrix_setter_b, db->type, for_block);
#undef x_block
		}
#undef GALL
#undef G125
#undef G075
#undef G025
	}
#undef for_block
}
