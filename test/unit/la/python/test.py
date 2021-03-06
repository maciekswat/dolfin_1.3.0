"""Unit tests for the linear algebra interface"""

# Copyright (C) 2008 Johan Hake
#
# This file is part of DOLFIN.
#
# DOLFIN is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# DOLFIN is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with DOLFIN. If not, see <http://www.gnu.org/licenses/>.
#
# First added:  2008-09-30
# Last changed: 2013-08-07

import unittest
from dolfin import *
import sys


mesh = UnitSquareMesh(3, 3)

class AbstractBaseTest(object):
    count = 0
    def setUp(self):
        if self.backend != "default":
            parameters.linear_algebra_backend = self.backend
        type(self).count += 1
        if type(self).count == 1:
            # Only print this message once per class instance
            print "\nRunning:",type(self).__name__

    def assemble_matrices(self, use_backend=False):
        " Assemble a pair of matrices, one (square) MxM and one MxN"

        V = FunctionSpace(mesh, "Lagrange", 2)
        W = FunctionSpace(mesh, "Lagrange", 1)

        v = TestFunction(V)
        u = TrialFunction(V)
        s = TrialFunction(W)

        # Forms
        a = dot(grad(u),grad(v))*dx
        b = v*s*dx

        if use_backend:
            if self.backend == "uBLAS":
                backend = getattr(cpp, self.backend+self.sub_backend+'Factory').instance()
            else:
                backend = getattr(cpp, self.backend+'Factory').instance()
            return assemble(a, backend=backend), assemble(b, backend=backend)
        else:
            return assemble(a), assemble(b)

    def assemble_vectors(self):
        V = FunctionSpace(mesh, "Lagrange", 2)
        W = FunctionSpace(mesh, "Lagrange", 1)

        v = TestFunction(V)
        t = TestFunction(W)

        return assemble(v*dx), assemble(t*dx)

    def test_vector(self):
        from numpy import ndarray, linspace, array, fromiter
        from numpy import int,int0,int16,int32,int64
        from numpy import uint,uint0,uint16,uint32,uint64
        v,w = self.assemble_vectors()

        # Get local ownership range (relevant for parallel vectors)
        n0, n1 = v.local_range()
        distributed = True
        if (n1 - n0) == v.size():
            distributed = False

        # Test set and access with different integers
        ind = 2
        for t in [int,int16,int32,int64,uint,uint0,uint16,uint32,uint64,int0,long]:
            v[t(ind)] = 2.0
            if v.owns_index(t(ind)): self.assertAlmostEqual(v[t(ind)], 2.0)

        A = v.copy()
        B = as_backend_type(v.copy())
        if A.owns_index(5): self.assertAlmostEqual(A[5], B[5])

        B *= 0.5
        A *= 2
        if A.owns_index(5): self.assertAlmostEqual(A[5], 4*B[5])

        B /= 2
        A /= 0.5
        if A.owns_index(5): self.assertAlmostEqual(A[5], 16*B[5])

        if n0 <= 5 and 5 < n1:
            val1 = A[5]
            val2 = B[5]
        A += B
        if A.owns_index(5): self.assertAlmostEqual(A[5], val1+val2)

        A -= B
        if A.owns_index(5): self.assertAlmostEqual(A[5], val1)

        C = 16*B
        if A.owns_index(5): self.assertAlmostEqual(A[5],C[5])

        D = (C + B)*5
        if A.owns_index(5): self.assertAlmostEqual(D[5], (val1+val2)*5)

        F = (A-B)/4
        if A.owns_index(5): self.assertAlmostEqual(F[5], (val1-val2)/4)

        A.axpy(100,B)
        if A.owns_index(5): self.assertAlmostEqual(A[5], val1+val2*100)

        A2 = A.array()
        self.assertTrue(isinstance(A2,ndarray))
        self.assertEqual(A2.shape, (n1-n0,))
        if A.owns_index(5): self.assertAlmostEqual(A2[5], A[5])
        self.assertAlmostEqual(MPI.sum(A2.sum()), A.sum())

        B2 = B.array()
        # TODO: test strides in parallel also
        if not distributed:
            A[1:16:2] = B[1:16:2]
            A2[1:16:2] = B2[1:16:2]
            self.assertAlmostEqual(A2[1], A[1])


        ind = [1,3,6,9,15,20,24,28,32,40,50,60,70,100000]

        # Extract owned indices
        ind = [i for i in ind if v.owns_index(i)]
        ind1 = array(ind, 'i')
        ind2 = array(ind, 'I')

        # Build indices with local numbering (for indexing arrays)
        ind0 = [i-n0 for i in ind]
        ind3 = list(array(ind0,'I'))

        if len(ind2)>0:
          A[ind2] = ind2
        else:
          A.apply('insert') # workaround to issue 54
        A2[ind3] = ind2

        G  = A[ind]
        G1 = A[ind1]
        G2 = A2[ind0]

        G3 = A[A > 1]
        G4 = A2[A2 > 1]

        if not distributed:
            A3 = fromiter(A, "d")

        if A.owns_index(15):
            a = A[15]
        b = 1.e10

        self.assertAlmostEqual(G1.sum(), G.sum())
        self.assertAlmostEqual(G2.sum(), G.sum())
        self.assertEqual(len(G3), len(G4))
        self.assertAlmostEqual(G3.sum(), G4.sum())
        if A.owns_index(len(A)-1): self.assertEqual(A[-1], A[len(A)-1])
        if A.owns_index(0): self.assertEqual(A[-len(A)], A[0])
        self.assertEqual(len(ind),len(G))
        self.assertTrue(all(val==G[i] for i, val in enumerate(G)))
        self.assertTrue((G==G1).all())
        self.assertTrue((G<=G1).all())
        self.assertTrue((G>=G1).all())
        self.assertFalse((G<G1).any())
        self.assertFalse((G>G1).any())
        if A.owns_index(15): self.assertTrue(a in A)
        self.assertTrue(b not in A)
        if not distributed:
            self.assertTrue((A3==A2).all())

        X = A==A                 # operator== returns array of global size with Falses at not-owned items
        for i in range(len(X)):  # gather X, because of issue 54
            X[i] = MPI.max(float(X[i]))
        A[:] = X
        self.assertTrue(A.sum()==len(A))

        if not distributed: # issue 54
            A[:] = A2
            self.assertTrue((A==A2).all())

        H  = A.copy()
        H._assign(0.0)
        H[ind] = G

        C[:] = 2
        D._assign(2)
        if C.owns_index(0): self.assertAlmostEqual(C[0], 2)
        if C.owns_index(len(A)-1): self.assertAlmostEqual(C[-1], 2)
        self.assertAlmostEqual(C.sum(), D.sum())

        C[ind] = 3
        self.assertAlmostEqual(C[ind].sum(), 3*len(ind))

        def wrong_index(ind):
            A[ind]

        self.assertRaises(RuntimeError, wrong_index, (-len(A)-1))
        self.assertRaises(RuntimeError, wrong_index, (len(A)+1))
        self.assertRaises(TypeError, wrong_index, ("jada"))
        self.assertRaises(TypeError, wrong_index, (.5))
        self.assertRaises(RuntimeError, wrong_index, ([-len(A)-1, 2]))
        self.assertRaises(RuntimeError, wrong_index, ([len(A), 2]))

        def wrong_dim(ind0, ind1):
            A[ind0] = B[ind1]

        self.assertRaises(RuntimeError, wrong_dim, [0,2], [0,2,4])
        self.assertRaises(RuntimeError, wrong_dim, [0,2], slice(0,4,1))
        #self.assertRaises(TypeError, wrong_dim, 0, slice(0,4,1))

        # Tests bailout for these choices
        if self.backend == "uBLAS" and sys.version_info[0]==2 and \
               sys.version_info[1]==6:
            return

        A2 = A.array()
        A *= B
        A2 *= B2
        I = A*B
        I2 = A2*B2
        self.assertAlmostEqual(A.sum(), MPI.sum(A2.sum()))
        self.assertAlmostEqual(I.sum(), MPI.sum(I2.sum()))

        def wrong_assign(A, ind):
            A[ind[::2]] = ind[::2]

        if len(ind[::2]) > 1:
            self.assertRaises(RuntimeError, wrong_assign, A, ind2)


    def test_matrix_vector(self, use_backend=False):
        from numpy import dot, absolute

        # Tests bailout for this choice
        if self.backend == "uBLAS" and not use_backend:
            return

        A,B = self.assemble_matrices(use_backend)
        v,w = self.assemble_vectors()

        # Get local ownership range (relevant for parallel vectors)
        n0, n1 = v.local_range()
        distributed = True
        if (n1 - n0) == v.size():
            distributed = False

        # Reference values
        v_norm  = 0.181443684651
        w_norm  = 0.278394377377
        A_norm  = 31.947874212
        B_norm  = 0.11052313564
        Av_norm = 0.575896483442
        Bw_norm = 0.0149136743079
        Cv_norm = 0.00951459156865

        self.assertAlmostEqual(v.norm('l2'), v_norm)
        self.assertAlmostEqual(w.norm('l2'), w_norm)
        self.assertAlmostEqual(A.norm('frobenius'), A_norm)
        self.assertAlmostEqual(B.norm('frobenius'), B_norm)

        u = A*v

        self.assertTrue(isinstance(u, type(v)))
        self.assertEqual(len(u), len(v))

        # Test basic square matrix multiply results

        self.assertAlmostEqual(v.norm('l2'), v_norm)
        self.assertAlmostEqual(u.norm('l2'), Av_norm)

        # Test rectangular matrix multiply results

        self.assertAlmostEqual((B*w).norm('l2'), Bw_norm)

        # Test transpose multiply (rectangular)

        x = Vector()
        if self.backend == 'uBLAS':
            self.assertRaises(RuntimeError, B.transpmult, v, x)
        else:
            B.transpmult(v, x)
            self.assertAlmostEqual(x.norm('l2'), Cv_norm)

        # Miscellaneous tests

        u2 = 2*u - A*v
        if u2.owns_index(4): self.assertAlmostEqual(u2[4], u[4])

        u3 = 2*u + -1.0*(A*v)
        if u3.owns_index(4): self.assertAlmostEqual(u3[4], u[4])

        if not distributed:
            v_numpy = v.array()
            A_numpy = A.array()

            u_numpy = dot(A_numpy,v_numpy)
            u_numpy2 = A*v_numpy

            self.assertTrue(absolute(u.array() - u_numpy).sum() < DOLFIN_EPS*len(v))
            self.assertTrue(absolute(u_numpy2 - u_numpy).sum() < DOLFIN_EPS*len(v))

    def test_matrix_vector_with_backend(self):
        self.test_matrix_vector(True)

class DataNotWorkingTester:
    def test_matrix_data(self):
        A,B = self.assemble_matrices()
        self.assertRaises(RuntimeError,A.data)

        A = as_backend_type(A)
        self.assertRaises(RuntimeError,A.data)

    def test_vector_data(self):
        v,w = self.assemble_vectors()
        self.assertRaises(RuntimeError,v.data)

        v = as_backend_type(v)
        def no_attribute():
            v.data()
        self.assertRaises(AttributeError,no_attribute)

if has_linear_algebra_backend("PETSc"):
    class PETScTester(DataNotWorkingTester, AbstractBaseTest, unittest.TestCase):
        backend    = "PETSc"

if has_linear_algebra_backend("Epetra"):
    class EpetraTester(DataNotWorkingTester, AbstractBaseTest, unittest.TestCase):
        backend    = "Epetra"

if MPI.num_processes() == 1:
    class uBLASSparseTester(AbstractBaseTest, unittest.TestCase):
        backend     = "uBLAS"
        sub_backend = "Sparse"

    class uBLASDenseTester(AbstractBaseTest, unittest.TestCase):
        backend     = "uBLAS"
        sub_backend = "Dense"

    if has_linear_algebra_backend("PETScCusp"):
        class PETScCuspTester(DataNotWorkingTester, AbstractBaseTest, unittest.TestCase):
            backend    = "PETScCusp"

if __name__ == "__main__":

    # Turn off DOLFIN output
    #set_log_active(False);

    print ""
    print "Testing basic PyDOLFIN linear algebra operations"
    print "------------------------------------------------"
    unittest.main()
