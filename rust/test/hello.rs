fn main() {
    let sum: i32 = (1..=10).sum();
    println!("hello from rustc, sum(1..=10) = {sum}");
    assert_eq!(sum, 55);
}
