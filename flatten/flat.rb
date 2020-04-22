class Array
  def flat
    flat_items(self, [])
  end

  private
    def flat_items(obj, flattened)
      if not obj.is_a? Enumerable
        flattened << obj
      else
        obj.each {|o| flat_items(o, flattened)}
      end

      flattened
    end
end
