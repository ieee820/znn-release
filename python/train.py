#!/usr/bin/env python
__doc__ = """

Jingpeng Wu <jingpeng.wu@gmail.com>, 2015
"""
import numpy as np
import pyznn
import emirt
import time
import matplotlib.pylab as plt
import front_end
import cost_fn

#%% parameters
ftrn = "/usr/people/jingpeng/seungmount/research/Jingpeng/43_zfish/fish_train/Merlin_raw2.tif"
flbl = "/usr/people/jingpeng/seungmount/research/Jingpeng/43_zfish/fish_train/ExportLabels_32bit_Merlin2.tif"
fnet_spec = '../networks/srini2d.znn'

# mode
dp_type = 'affinity'
# learning rate
eta = 0.01
# momentum
momentum = 0


# output size
outsz = np.asarray([1,20,20])
# number of threads
num_threads = 7

# softmax
is_softmax = False

# rebalance
is_rebalance = True

# cost function
cfn = cost_fn.binomial_cross_entropy

#%% prepare input
vol_org = emirt.io.imread(ftrn).astype('float32')
lbl_org = emirt.io.imread(flbl).astype('float32')
# normalize the training volume
vol_org = vol_org / 255

#%% create and initialize the network
print "output volume size: {}x{}x{}".format(outsz[0], outsz[1], outsz[2])
net = pyznn.CNet(fnet_spec, outsz[0],outsz[1],outsz[2],num_threads)
net.set_eta( eta / float(outsz[0] * outsz[1] * outsz[2]) )
net.set_momentum( momentum )

# compute inputsize and get input
fov = np.asarray(net.get_fov())
print "field of view: {}x{}x{}".format(fov[0],fov[1], fov[2])
insz = fov + outsz - 1

err = 0;
cls = 0;
# get gradient
plt.ion()
plt.show()

start = time.time()
for i in xrange(1,1000000):
    vol_in, lbl_outs = front_end.get_sample( vol_org, insz, lbl_org, outsz, type=dp_type )
    inputs = list()
    inputs.append( np.ascontiguousarray(vol_in) )
    # forward pass
    props = net.forward( inputs )

    # softmax
    if is_softmax:
        props = front_end.softmax(props)

    # cost function and accumulate errors
    cerr, ccls, grdts = cfn( props, lbl_outs )

    # rebalance
    if is_rebalance:
        grdts = cost_fn.rebalance(grdts, lbl_outs)
    err = err + cerr
    cls = cls + ccls

    if i%1000==0:
        err = err / float(1000 * outsz[0] * outsz[1] * outsz[2])
        cls = cls / float(1000 * outsz[0] * outsz[1] * outsz[2])

        # time
        elapsed = time.time() - start
        print "iteration %d,    sqerr: %.3f,    clserr: %.3f,   elapsed: %.1f s"\
                %(i, err, cls, elapsed)
        # real time visualization
        norm_prop = emirt.volume_util.norm(props[1])
        norm_lbl_out = emirt.volume_util.norm( lbl_outs[1] )
        abs_grdt = np.abs(grdts[1])

        plt.subplot(221),   plt.imshow(vol_in[0,:,:],   cmap='gray')
        plt.xlabel('input')
        plt.subplot(222),   plt.imshow(norm_prop[0,:,:],    interpolation='nearest', cmap='gray')
        plt.xlabel('inference')
        plt.subplot(223),   plt.imshow(norm_lbl_out[0,:,:], interpolation='nearest', cmap='gray')
        plt.xlabel('lable')
        plt.subplot(224),   plt.imshow(abs_grdt[0,:,:],     interpolation='nearest', cmap='gray')
        plt.xlabel('gradient')
        plt.pause(1)

        # reset time
        start = time.time()
        # reset err and cls
        err = 0
        cls = 0


    # run backward pass
    net.backward( grdts )


#%% visualization
#com = emirt.show.CompareVol((vol_in, lbl_out))
#com.vol_compare_slice()