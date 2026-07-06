fn main() {
    let v: Vec<i32> = (1..=10).collect();
    let sum: i32 = v.iter().sum();
    println!("hello from cargo, sum = {sum}");
    assert_eq!(sum, 55);
}
