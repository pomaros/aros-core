# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
import unittest
from oracles import inspect, expected_rejection, serial_failure, FIRMWARE_DIAGNOSTICS
GOOD='''[CPU21] guest command begin
[CPU21] begin usage_scale=4294967295 window_us=250000 cpus=1
[CPU21] windows base_cpu=0 base_usage=0 busy_cpu=900000 busy_usage=3800000000 idle_cpu=900100 idle_usage=400000 busy_us=1000020 idle_us=1000030 iterations=1234567
[CPU21] current before_cpu=15000 after_cpu=65001 elapsed_us=50001
[CPU21] churn created=32 exited=32 live_queries=32
[CPU21] sleep unqualified
[CPU21] PASS
[CPU21] guest command PASS
[B2Q] AROS startup contract PASS
'''
class Oracle(unittest.TestCase):
    def check(self,serial=GOOD,host='',trace='',exitcode=0,orderly=True,timedout=False):
        return inspect(serial,host,trace,3,exitcode,orderly,timedout)['result']
    def test_good(self): self.assertEqual(self.check(),'PASS')
    def test_accounting_negatives(self):
        for old,new in [('busy_cpu=900000','busy_cpu=0'),('busy_usage=3800000000','busy_usage=0'),('busy_usage=3800000000','busy_usage=1500000000'),('idle_cpu=900100','idle_cpu=10'),('idle_usage=400000','idle_usage=3800000000'),('after_cpu=65001','after_cpu=15000'),('elapsed_us=50001','elapsed_us=1'),('exited=32','exited=31')]:
            with self.subTest(old=old): self.assertEqual(self.check(GOOD.replace(old,new)),'FAIL')
    def test_inflated_counters(self):
        self.assertEqual(self.check(GOOD.replace('busy_cpu=900000','busy_cpu=9000000').replace('idle_cpu=900100','idle_cpu=9000100').replace('busy_usage=3800000000','busy_usage=4294967295')),'FAIL')
        self.assertEqual(self.check(GOOD.replace('after_cpu=65001','after_cpu=650001')),'FAIL')
    def test_firmware_exception_boundary(self):
        self.assertEqual(self.check('Tpm2GetCapabilityPcrs fail!\n'+GOOD),'PASS')
        for marker in ('[B2A] start','[VM] start','[B2Q] start','[AROSBOOT] start'):
            self.assertEqual(self.check(marker+'\nTpm2GetCapabilityPcrs fail!\n'+GOOD),'FAIL')
    def test_known_firmware_is_exact_and_pre_loader(self):
        for line in FIRMWARE_DIAGNOSTICS:
            self.assertFalse(serial_failure(line+'\n'))
            self.assertTrue(serial_failure('AROSBOOTAA64: starting generic UEFI loader\n'+line+'\n'))
        self.assertTrue(serial_failure('Error: Image at 000DEADBEEF start failed: Aborted\n'))
    def test_expected_rejection_is_narrow(self):
        bad='[CPU21] begin\n[CPU21] FAIL busy/idle accounting contract\n[CPU21] guest command FAIL\n[B2Q] FAIL: stage-two startup\n'
        self.assertTrue(expected_rejection(bad))
        for suffix in ('PANIC\n','ERROR\n','[CPU21] PASS\n','[CPU21] FAIL busy/idle accounting contract\n'):
            self.assertFalse(expected_rejection(bad+suffix))
        self.assertFalse(expected_rejection(bad.replace('busy/idle accounting contract','task exit churn')))
    def test_rejection_requires_complete_ordered_failure_chain(self):
        chain=['[CPU21] FAIL busy/idle accounting contract',
               '[CPU21] guest command FAIL','[B2Q] FAIL: stage-two startup']
        self.assertTrue(expected_rejection('\n'.join(chain)+'\n'))
        for index in range(len(chain)):
            self.assertFalse(expected_rejection('\n'.join(chain[:index]+chain[index+1:])))
        self.assertFalse(expected_rejection('\n'.join(reversed(chain))))
        self.assertFalse(expected_rejection('prefix '+ '\n'.join(chain)))
    def test_lifecycle_negatives(self):
        for kwargs in ({'serial':GOOD+'PANIC\n'},{'host':'ERROR'},{'trace':None},{'trace':'unimplemented'},{'exitcode':-11},{'orderly':False},{'timedout':True},{'serial':GOOD+GOOD}):
            with self.subTest(kwargs=kwargs): self.assertEqual(self.check(**kwargs),'FAIL')
if __name__=='__main__': unittest.main()
