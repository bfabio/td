require_relative "flat"

require "benchmark"
require "test/unit"

class TestFlat < Test::Unit::TestCase
  def test_flat
    assert_equal([], [].flat)
    assert_equal([], [[]].flat)
    assert_equal([1, 2, 3, 4], [[1, 2, [3]], 4].flat)
    assert_equal([1, 2, 3, 2, 3, 4, 4, 5, 5, 1, 2, 3, 4],[[[1, [2, 3]],[2, 3, [4, [4, [5, 5]], [1, 2, 3]]], [4]], []].flat)
    assert_equal([1, 2, 3, nil],[1, 2, 3, [nil]].flat)

    puts "Benchmarking..."

    n = 1_000
    array = (0..10_000).to_a << [(0..10_000).to_a] << [[(0..10_000).to_a]]
    Benchmark.bmbm do |b|
      b.report("Array#flat")    { n.times do; array.flat; end }
      b.report("Array#flatten") { n.times do; array.flatten; end }
    end
  end
end
