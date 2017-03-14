#include "colorstereonet.h"

#include "operators/slackprop.h"

#include "iu/ndarray/ndarray.h"
#include "iu/ndarray/ndarray_iu.h"


#include "utils.h"


ColorStereoNet::ColorStereoNet(unsigned int numLayers, unsigned int in, unsigned int ic, unsigned int ih, unsigned int iw) :
    StereoNet(numLayers, in, ic, ih, iw)
{
}

ColorStereoNet::~ColorStereoNet()
{
}

iu::TensorGpu_32f *ColorStereoNet::predict(iu::ImageGpu_32f_C4 *d_left, iu::ImageGpu_32f_C4 *d_right)
{
	if(m_ic == 5)
	{
		return predictXY(d_left, d_right);
	}
	else
	{
	// adjust inputs (contiguous memory + mem-layout)
	int in = 1;
	int ic = 3;

	iu::TensorGpu_32f  d_inputLeft(in, ic, d_left->height(), d_left->width(), iu::TensorGpu_32f::MemoryLayout::NCHW);
	iu::TensorGpu_32f d_inputRight(in, ic, d_right->height(), d_right->width(), iu::TensorGpu_32f::MemoryLayout::NCHW);
//
//	auto s = d_inputLeft.ref().subdim<0>(0).subrange({0,0,0},{3,5,5});
//	ndarray<float,3> a;
//	a.create<memory::CPU>(s);
//	a << s;
//	std::cout << "s=\n" << s << "\n";
//	print_array("a = ",a, 0);

	d_inputLeft.ref().subdim<0>(0) << d_left->ref().unpack().subrange({0,0,0},{3,d_left->width(),d_left->height()}).permute_dims({0,2,1});
	d_inputRight.ref().subdim<0>(0) << d_right->ref().unpack().subrange({0,0,0},{3,d_right->width(),d_right->height()}).permute_dims({0,2,1});

//	save(d_inputLeft, "/tmp/im0.npy");
//	save(d_inputRight, "/tmp/im1.npy");

	iu::IuCudaTimer cut;
	if (m_verbose)
		cut.start();

	cuda::makeRgbZeroMean(d_inputLeft);
	cuda::makeRgbUnitStd(d_inputLeft, true);

	cuda::makeRgbZeroMean(d_inputRight);
	cuda::makeRgbUnitStd(d_inputRight, true);

	if (m_verbose)
		std::cout << "Elapsed time RGB (zero mean, unit variance): " << cut.elapsed() << std::endl;

	if(m_numLayers == 7 && m_pairwiseOps.size() > 0)
	{
		m_pwInput = new iu::TensorGpu_32f(m_in, 3, m_ih - 4, m_iw - 4, d_inputLeft.memoryLayout());
		cuda::cropTensor(d_inputLeft, *m_pwInput, 2);
//		save(d_inputLeft, "/tmp/im0.npy");
//		save(*m_pwInput, "/tmp/pwin.npy");
	}

//	save(d_inputLeft, "/tmp/im0.npy");
//	save(d_inputRight, "/tmp/im1.npy");

	return performPrediction(&d_inputLeft, &d_inputRight);
	}
}

iu::TensorGpu_32f *ColorStereoNet::predictXY(iu::ImageGpu_32f_C4 *d_left, iu::ImageGpu_32f_C4 *d_right)
{
	// adjust inputs (contiguous memory + mem-layout)
	int in = 1;
	int ic = 3;

	iu::TensorGpu_32f  d_inputLeft_rgb(in, ic, d_left->height(), d_left->width(), iu::TensorGpu_32f::MemoryLayout::NCHW);
	iu::TensorGpu_32f d_inputRight_rgb(in, ic, d_right->height(), d_right->width(), iu::TensorGpu_32f::MemoryLayout::NCHW);

	d_inputLeft_rgb.ref().subdim<0>(0) << d_left->ref().unpack().subrange({0,0,0},{3,d_left->width(),d_left->height()}).permute_dims({0,2,1});
	d_inputRight_rgb.ref().subdim<0>(0) << d_right->ref().unpack().subrange({0,0,0},{3,d_right->width(),d_right->height()}).permute_dims({0,2,1});

	iu::IuCudaTimer cut;
	if (m_verbose)
		cut.start();

	cuda::makeRgbZeroMean(d_inputLeft_rgb);
	cuda::makeRgbUnitStd(d_inputLeft_rgb, true);

	cuda::makeRgbZeroMean(d_inputRight_rgb);
	cuda::makeRgbUnitStd(d_inputRight_rgb, true);

	// add x- and y-position
	//xx, yy = np.meshgrid(np.arange(im0.shape[3]), np.arange(im0.shape[2]))
    //xx = (xx.astype('float32') - (im0.shape[3]) / 2.) / im0.shape[3]
    //yy = (yy.astype('float32') - (im0.shape[2]) / 2.) / im0.shape[2]
	iu::TensorCpu_32f xy_pos(in, 2, d_left->height(), d_left->width());
	for(int y = 0; y < xy_pos.height(); ++y)
	{
		for(int x = 0; x < xy_pos.width(); ++x)
		{
			float x_normalized = (static_cast<float>(x) - (static_cast<float>(d_left->width()) / 2.0)) / static_cast<float>(d_left->width());
			float y_normalized = (static_cast<float>(y) - (static_cast<float>(d_left->height()) / 2.0)) / static_cast<float>(d_left->height());
			xy_pos.setPixel(x_normalized, 0, 0, x, y);
			xy_pos.setPixel(y_normalized, 0, 1, x, y);
		}
	}

	iu::TensorGpu_32f d_xy_pos(in, 2, d_left->height(), d_left->width());
	iu::copy(&xy_pos, &d_xy_pos);

	iu::TensorGpu_32f d_inputLeft(in, ic + 2, d_left->height(), d_left->width(), iu::TensorGpu_32f::MemoryLayout::NCHW);
	cuda::concatOver_c_dim(d_inputLeft_rgb, d_xy_pos, d_inputLeft);

	iu::TensorGpu_32f d_inputRight(in, ic + 2, d_right->height(), d_right->width(), iu::TensorGpu_32f::MemoryLayout::NCHW);
	cuda::concatOver_c_dim(d_inputRight_rgb, d_xy_pos, d_inputRight);


//	save(d_inputLeft, "/tmp/left.npy");
//	save(d_inputRight, "/tmp/right.npy");


	if (m_verbose)
		std::cout << "Elapsed time RGB (zero mean, unit variance): " << cut.elapsed() << std::endl;

	if(m_numLayers == 7 && m_pairwiseOps.size() > 0)
	{
		m_pwInput = new iu::TensorGpu_32f(m_in, 3, m_ih - 4, m_iw - 4, d_inputLeft_rgb.memoryLayout());
		cuda::cropTensor(d_inputLeft_rgb, *m_pwInput, 2);
//		save(d_inputLeft, "/tmp/im0.npy");
//		save(*m_pwInput, "/tmp/pwin.npy");
	}

//	save(d_inputLeft, "/tmp/im0.npy");
//	save(d_inputRight, "/tmp/im1.npy");

	return performPrediction(&d_inputLeft, &d_inputRight);
}

iu::TensorGpu_32f *ColorStereoNet::predictXY(iu::ImageCpu_32f_C4 *left, iu::ImageCpu_32f_C4 *right)
{
	iu::ImageGpu_32f_C4 d_left(left->width(), left->height());
	iu::copy(left, &d_left);

	iu::ImageGpu_32f_C4 d_right(right->width(), right->height());
	iu::copy(right, &d_right);

	return predictXY(&d_left, &d_right);
}

iu::TensorGpu_32f *ColorStereoNet::predict(iu::ImageCpu_32f_C4 *left, iu::ImageCpu_32f_C4 *right)
{
	iu::ImageGpu_32f_C4 d_left(left->width(), left->height());
	iu::copy(left, &d_left);

	iu::ImageGpu_32f_C4 d_right(right->width(), right->height());
	iu::copy(right, &d_right);

	return predict(&d_left, &d_right);
}